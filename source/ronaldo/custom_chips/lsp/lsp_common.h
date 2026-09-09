#pragma once

#include <cstdint>
#include <cstring>

// Fujitsu MB87837 effects processor used by the SC-88Pro and SC-8850.
// Executes 384 instructions per sample with 24-bit arithmetic, a 128-word
// internal ring and 64 Kiwords of external delay RAM.
// Immediate MUL uses a unit mantissa; its coefficient byte contains flags.

namespace lspLib
{
	static constexpr uint32_t ProgramWords    = 384;
	static constexpr uint32_t IramProgramBase = 0x80;		// host address of program word 0
	static constexpr uint32_t HostIramSize    = 0x200;		// host address space: ring + program
	static constexpr uint32_t DataRingSize    = 0x80;
	static constexpr uint32_t DataRingMask    = DataRingSize - 1;
	static constexpr uint32_t EramSize        = 0x10000;
	static constexpr uint32_t EramMask        = EramSize - 1;

	// The chip processes a stereo frame as two interleaved halves of one
	// program: audio in is the right channel until an audio-in instruction at
	// or past this point, audio out written before it is the right channel.
	static constexpr uint32_t ChannelSplit = ProgramWords / 2;

	constexpr int32_t clamp24(const int64_t _v)
	{
		if(_v > 0x7fffff) return 0x7fffff;
		if(_v < -0x800000) return -0x800000;
		return static_cast<int32_t>(_v);
	}

	constexpr int32_t signExtend24(const int32_t _x)
	{
		return static_cast<int32_t>(static_cast<uint32_t>(_x) << 8) >> 8;
	}

	enum class Op : uint8_t { Skip, Mac, Mul, Special };

	// Write control: which accumulator history value a store or special reads.
	enum class Src : uint8_t { None, ASat, BSat, ARaw };

	// Jump condition, carried by the instruction AFTER the special that set it.
	enum class Jump : uint8_t { None, Always, IfNegative, IfNonNegative };

	// Special-register slots (rr & 0x1f)
	enum Slot : uint8_t
	{
		SlotJumpIfNegative    = 0x0d,
		SlotJumpIfNonNegative = 0x0e,
		SlotJumpAlways        = 0x0f,
		SlotEramWriteLatch    = 0x10,
		SlotEramTapAndCoef1   = 0x13,
		SlotMulCoef1          = 0x14,
		SlotMulCoef2          = 0x15,
		SlotAudioOut          = 0x18,
		SlotEramRead0         = 0x1a,	// ..0x1d
		SlotAudioIn           = 0x1e,
	};

	// One decoded instruction. Trivially copyable: a compile worker snapshots
	// the whole array. The coefficient byte and the ERAM base address are NOT
	// consumed from here by the engines — they read the program's live tables
	// (coefs[] / eramAddr[]) so a host edit of either needs no re-decode. The
	// one exception is a coefficient crossing zero on an accumulating op,
	// which flips zeroCoef and is classified as structural.
	struct LSPInstr
	{
		uint8_t  ii = 0;
		uint8_t  rr = 0;
		int8_t   cc = 0;

		Op       op = Op::Skip;
		Src      src = Src::None;
		uint8_t  memOffs = 0;
		uint8_t  scaler = 7;		// post-multiply shift, 5 or 7
		uint8_t  immShift = 0;		// memOffs 1-4 select an immediate: 7/12/17/22, 0 = none

		// Accumulator write, shared by every op that has one
		bool     writesAcc = false;
		bool     accB = false;		// destination B (else A)
		bool     replace = false;	// replace instead of accumulate

		// MAC
		bool     abs = false;
		bool     zeroCoef = false;	// accumulates a zero coefficient: the accumulator is unchanged

		// MUL
		bool     mulLower = false;
		bool     mulNegate = false;
		bool     mulCoef2 = false;
		bool     mulZero = false;	// coefficient byte 0 forces a zero product

		// Special
		uint8_t  slot = 0;
		bool     imm50d0 = false;	// slot 0x10 without write control: immediate accumulate
		uint8_t  prevMem = 0;		// previous instruction's memOffs, operand of 50/d0

		// Control flow
		Jump     jump = Jump::None;
		uint16_t jumpDest = 0;
		bool     isJumpTarget = false;

		// External RAM access committed by this instruction
		bool     eramRead = false;
		bool     eramWrite = false;
		bool     eramSecondTap = false;

		// Flattened accumulator plan (see LSPProgram::assignRegs). Registers
		// 0-2 belong to A, 3-5 to B; stateIn/stateOut list, per accumulator,
		// the registers holding v(t-3), v(t-2), v(t-1) before / after the
		// instruction, so a store reads stateIn[base], an accumulate reads
		// stateIn[base+2] and the result lands in destReg.
		uint8_t  stateIn[6] = {0, 1, 2, 3, 4, 5};
		uint8_t  stateOut[6] = {0, 1, 2, 3, 4, 5};
		uint8_t  destReg = 0;

		uint8_t readReg() const { return stateIn[src == Src::BSat ? 3 : 0]; }
		uint8_t liveReg() const { return stateIn[accB ? 5 : 2]; }
	};

	// Everything that changes per sample. One image shared by the interpreter
	// and the JIT so an engine switch resumes from identical state. Scalars
	// first so their offsets stay small for the JIT; the ERAM is why this is
	// heap-owned.
	struct LSPRuntime
	{
		int32_t  audioInL = 0;
		int32_t  audioInR = 0;
		int32_t  audioOutL = 0;
		int32_t  audioOutR = 0;

		// Accumulator pipeline, canonical form: A = v(t-2), v(t-1), v(t) in
		// accs[0..2], B in accs[3..5]. A store at t reads v(t-3), i.e. accs[0]
		// / accs[3] before the shift.
		int32_t  accs[6] = {0};

		int32_t  eramReadValue = 0;
		int32_t  eramWriteLatch = 0;		// slot 0x10
		int32_t  eramSecondTapOffs = 0;		// slot 0x13
		int32_t  multiplCoef1 = 0;			// slot 0x14
		int32_t  multiplCoef2 = 0;			// slot 0x15
		int32_t  audioOut = 0;				// slot 0x18
		int32_t  audioIn = 0;				// slot 0x1e
		int32_t  jumpPending = 0;			// set by a jump special, consumed (and cleared) by the next slot's jump

		uint8_t  bufferPos = 0;				// ring rotation, decremented per sample
		uint16_t eramPos = 0;

		int32_t  iram[DataRingSize] = {0};
		int32_t  eram[EramSize] = {0};

		void clear()
		{
			std::memset(this, 0, sizeof(*this));
		}

		void clearEram()
		{
			std::memset(eram, 0, sizeof(eram));
			eramPos = 0;
			eramReadValue = 0;
		}

		int32_t readRing(const uint8_t _memOffs) const
		{
			return iram[(_memOffs + bufferPos) & DataRingMask];
		}

		void writeRing(const uint8_t _memOffs, const int32_t _value)
		{
			iram[(_memOffs + bufferPos) & DataRingMask] = _value;
		}

		// Both rings walk backwards one slot per sample.
		void endOfPass()
		{
			audioOutL = audioOut;
			bufferPos = static_cast<uint8_t>((bufferPos - 1) & DataRingMask);
			eramPos = static_cast<uint16_t>(eramPos - 1);
		}
	};
}
