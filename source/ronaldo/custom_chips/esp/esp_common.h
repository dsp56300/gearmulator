#pragma once

// Roland ESP (TC170C140AF) effects DSP. The chip contract is doc/esp/README.md.
// The engines run a whole frame per call: host writes land on frame boundaries
// and the IRQ pins are reported as per-frame edges the board replays.

#include <cstdint>
#include <cstring>
#include <memory>

// For the interpreter's per-clock path, which the compiler otherwise leaves as calls.
#if defined(_MSC_VER)
#	define ESP_ALWAYS_INLINE __forceinline
#else
#	define ESP_ALWAYS_INLINE inline __attribute__((always_inline))
#endif

namespace esp
{
	template <int32_t N> static constexpr int32_t se(int32_t x) { x <<= (32 - N); return x >> (32 - N); }

	static constexpr unsigned PRAM_SIZE = 768;		// executable words per core
	static constexpr unsigned PRAM_WORDS = 1024;	// host aperture per core (words)
	static constexpr unsigned IRAM_SIZE = 256, IRAM_MASK = IRAM_SIZE - 1;
	static constexpr unsigned FRAME_CLOCKS = 768;	// nominal DSP clocks per sample
	static constexpr unsigned NORMAL_DECODE_END = 756;	// slots 756/757 raw tail, 758..767 host tail
	static constexpr uint32_t ERAM_MASK_FULL = (1u << 19) - 1;
	// ERAM transaction stage (clocks after the command) at which a transfer commits.
	static constexpr unsigned ERAM_COMMIT_STAGE = 7, ERAM_COMMIT_STAGE_RAW_SINGLE = 6;
	// A compressed read shows its arriving low byte at S+5 only when the preceding
	// command is further away than this (see EramRuntime::tick).
	static constexpr unsigned ERAM_EARLY_READ_HOLD_GAP = 12;

	// Host transaction selected by 2003 bits 1:0.
	enum HostMode : uint8_t { kModeProgramWrite = 0, kModeCoefWrite = 1, kModeFieldWrite = 2, kModeReadSelect = 3 };

	// 2001.bit6 entry transform: each higher hexadecimal digit of a circular
	// position decrements when bit3 of the original lower digit is clear.
	static constexpr uint32_t freezePosition(uint32_t position, uint32_t mask)
	{
		uint32_t result = position & 15;
		for (unsigned shift = 4; shift < 20; shift += 4)
			result |= (((position >> shift) - ((position & (1u << (shift - 1))) == 0)) & 15) << shift;
		return result & mask;
	}

	static constexpr uint32_t programWordMask(unsigned wordIndex) { return wordIndex < PRAM_WORDS ? 0x00ffffff : 0x0fffffff; }

	// Ordinary right shift selected by word bits 9:8.
	static constexpr uint8_t shiftAmount(unsigned bits) { return (0x3567 >> ((bits & 3) << 2)) & 15; }

	// Ordered so the common arithmetic forms come first.
	enum class Op : uint8_t
	{
		Nop,			// all-zero word: pipelines advance, nothing else
		Mac,			// 00/04/10/14: A/B (+)= P(I)
		StoreSat,		// 08/0C/18/1C/58/5C: IRAM[m] = sat(A|B), then (+)= P(stored)
		GramRead,		// 20/24: source GRAM[m], shift 7/6, A/B by shift bit1
		GramStoreReplace,	// 28/2C: GRAM[m] = sat(A|B); replace A/B (shift bit1) from I
		Mul30,			// 30: runtime-coefficient multiply
		Special,		// 34: coefficient registers / specials / branches
		GramStoreAdd,	// 38/3C: GRAM[m] = sat(A|B); A += P(stored)
		StoreRaw,		// 40/44: IRAM[m] = raw24(A)
		StoreRect,		// 48/4C: IRAM[m] = max(0, sat(A))
		Cmp,			// 50
		Op54,			// 54: ordinary MAC (extra effect unknown)
		Invert,			// 60/64/70/74: source (~I) & 7FFFFF
		StoreFoldPos,	// 68/6C: store sat(A); source folded when >= 0
		StoreFoldNeg,	// 78/7C: store sat(A); source folded when < 0
	};

	// One decoded program word, so the raw word is never re-decoded on the hot path.
	struct Instr
	{
		// 20 bytes: the decode cache holds 2 x 1024 of these per chip.
		uint32_t word;
		int32_t constant;		// operand 1..4 substitution, 0 when the operand reads IRAM
		Op op;
		uint8_t opcode;			// raw five-bit opcode times four (00..7C)
		uint8_t mem;			// operand m
		uint8_t shiftBits;		// word bits 9:8
		uint8_t shift;			// resolved ordinary shift
		int8_t coef;			// signed immediate coefficient / flag byte
		uint8_t field;			// core1 ERAM control field (bits 27:23)
		uint8_t sub;			// opcode 34 sub-operation, m & F
		// Opcode 34 operands A0..AF: coefficient register index and which core
		// may write it (bit0 core0, bit1 core1); zero when not a register write.
		uint8_t coefIndex, coefWriteMask;
		bool accB : 1;			// destination accumulator B
		bool replace : 1;		// replace instead of accumulate
		bool storeB : 1;		// stored/source accumulator is B (store forms)
		bool requestsGram : 1;
		bool control : 1;		// opcode 34 control form: m >= C0 or m & CF == 45
		bool useConstant : 1;
		bool coefFromB : 1;

		Instr() { decode(0); }
		explicit Instr(uint32_t _word) { decode(_word); }

		void decode(uint32_t _word)
		{
			word = _word;
			field = (_word >> 23) & 31;
			const uint32_t w = _word & 0x7fffff;
			opcode = (w >> 16) & 0x7c;
			mem = (w >> 10) & 0xff;
			shiftBits = (w >> 8) & 3;
			shift = shiftAmount(shiftBits);
			coef = int8_t(w & 0xff);
			accB = replace = storeB = requestsGram = control = false;
			sub = mem & 0xf;
			coefIndex = coefWriteMask = 0;
			coefFromB = false;
			useConstant = mem >= 1 && mem <= 4;
			constant = useConstant ? (0x10 << (6 * (mem - 1))) : 0;	// 10, 400, 10000, 400000
			// Only an all-zero word is a NOP. A core1 word carrying nothing but
			// ERAM field bits still executes as a zero-product MAC.
			if (!_word) { op = Op::Nop; return; }
			switch (opcode)
			{
			case 0x00: op = Op::Mac; break;
			case 0x04: op = Op::Mac; replace = true; break;
			case 0x10: op = Op::Mac; accB = true; break;
			case 0x14: op = Op::Mac; accB = true; replace = true; break;
			case 0x08: op = Op::StoreSat; replace = true; break;
			case 0x0c: op = Op::StoreSat; replace = true; storeB = true; break;
			case 0x18: op = Op::StoreSat; replace = true; accB = true; break;
			case 0x1c: op = Op::StoreSat; replace = true; accB = true; storeB = true; break;
			case 0x58: op = Op::StoreSat; break;
			case 0x5c: op = Op::StoreSat; accB = true; storeB = true; break;
			case 0x20: op = Op::GramRead; accB = shiftBits & 2; shift = (shiftBits & 1) ? 6 : 7; requestsGram = true; break;
			case 0x24: op = Op::GramRead; accB = shiftBits & 2; shift = (shiftBits & 1) ? 6 : 7; replace = true; requestsGram = true; break;
			case 0x28: op = Op::GramStoreReplace; accB = shiftBits & 2; replace = true; requestsGram = true; break;
			case 0x2c: op = Op::GramStoreReplace; accB = shiftBits & 2; replace = true; storeB = true; requestsGram = true; break;
			case 0x30: op = Op::Mul30; accB = coef & 2; replace = !(coef & 1); break;
			case 0x34:
				op = Op::Special;
				control = mem >= 0xc0 || (mem & 0xcf) == 0x45;
				if (control) { accB = mem & 0x20; replace = mem & 0x10; }
				if (mem >= 0xa0 && mem < 0xb0)
				{
					// Shared reads, owned writes: core1 writes 0..2, core0 writes 3..5.
					coefIndex = (mem >> 1) & 7;
					coefFromB = mem & 1;
					coefWriteMask = coefIndex >= 6 ? 3 : coefIndex < 3 ? 2 : 1;
				}
				break;
			case 0x38: op = Op::GramStoreAdd; requestsGram = true; break;
			case 0x3c: op = Op::GramStoreAdd; storeB = true; requestsGram = true; break;
			case 0x40: op = Op::StoreRaw; break;
			case 0x44: op = Op::StoreRaw; replace = true; break;
			case 0x48: op = Op::StoreRect; break;
			case 0x4c: op = Op::StoreRect; replace = true; break;
			case 0x50: op = Op::Cmp; replace = true; break;
			case 0x54: op = Op::Op54; break;
			case 0x60: case 0x70: op = Op::Invert; break;
			case 0x64: case 0x74: op = Op::Invert; replace = true; break;
			case 0x68: op = Op::StoreFoldPos; break;
			case 0x6c: op = Op::StoreFoldPos; replace = true; break;
			case 0x78: op = Op::StoreFoldNeg; break;
			case 0x7c: op = Op::StoreFoldNeg; replace = true; break;
			default: op = Op::Mac; break;	// unreachable: every five-bit opcode is listed
			}
		}
	};

	static_assert(sizeof(Instr) == 20);

	static inline bool isBranch(const Instr& in) { return in.control && in.sub <= 3; }

	// The part of a word a compiled engine would bake into code. Ordinary
	// coefficient/shift edits are live data; bit9 still selects the accumulator
	// on GRAM instructions and the MUL30/special control bytes select
	// registers, transforms and branch targets (DMAC excepted, see below). A
	// zero word is a true NOP, distinct from a zero-product MAC (multiplier
	// history differs).
	static constexpr uint32_t structureOf(uint32_t word)
	{
		if (!word) return 0;
		const unsigned op = (word >> 16) & 0x7c;
		const unsigned mem = (word >> 10) & 255;
		// DMAC (special 6) reads its coefficient register index from the byte at
		// run time: firmware modulates it continuously.
		if (op == 0x30 || (op == 0x34 && (mem >= 0xc0 || (mem & 0xcf) == 0x45) && (mem & 0xf) != 6))
			return 0x80000000u | (word & 0x7fffffu);
		return 0x80000000u | (word & 0x7ffc00u) | ((op >= 0x20 && op <= 0x2c) ? word & 0x200u : 0);
	}

	// Signed 24-bit value with a signed 14-bit mantissa and two-bit exponent:
	// the 2003.bit6 ERAM codec.
	static constexpr uint16_t packEram(int32_t value)
	{
		value = se<24>(value);
		for (unsigned exponent = 0; exponent < 4; ++exponent)
		{
			const int32_t mantissa = value >> (4 + 2 * exponent);
			if (mantissa >= -8192 && mantissa <= 8191)
				return uint16_t((mantissa & 0x3fff) | (exponent << 14));
		}
		return 0;
	}
	static constexpr int32_t unpackEram(uint16_t value)
	{
		const int32_t mantissa = (value & 0x1fff) - (value & 0x2000);
		return mantissa * (1 << (4 + 2 * (value >> 14)));
	}
}
