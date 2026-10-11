// Tests for the ColdFire core.
//
// 1. Differential: random instructions whose ColdFire semantics are identical to the 68020's run on this
//    core and on Musashi (68020 mode) from the same state; registers, condition codes, PC and memory must
//    match. Known ColdFire differences are either not generated or masked, see the generators.
// 2. ColdFire specifics that Musashi cannot check: exception frame and RTE, REMS/REMU, ASL V flag,
//    (A7)+ byte step, divide by zero.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "coldfire/cfCpu.h"

#include "mc68k/mc68k.h"

namespace
{
	constexpr uint32_t g_memSize = 0x10000;
	constexpr uint32_t g_codeAddr = 0x0100;

	// Musashi on flat memory
	class RefCpu : public mc68k::Mc68k
	{
	public:
		std::vector<uint8_t> mem = std::vector<uint8_t>(g_memSize, 0);

		uint8_t read8(const uint32_t _addr) override { return mem[_addr & (g_memSize - 1)]; }
		uint16_t read16(const uint32_t _addr) override { return static_cast<uint16_t>((read8(_addr) << 8) | read8(_addr + 1)); }
		void write8(const uint32_t _addr, const uint8_t _v) override { mem[_addr & (g_memSize - 1)] = _v; }
		void write16(const uint32_t _addr, const uint16_t _v) override { write8(_addr, static_cast<uint8_t>(_v >> 8)); write8(_addr + 1, static_cast<uint8_t>(_v)); }
		uint16_t readImm16(const uint32_t _addr) override { return read16(_addr); }
	};
}

#define MC68K_CLASS RefCpu
#include "mc68k/musashiEntry.h"

namespace
{
	class RamBus final : public coldfire::Bus
	{
	public:
		std::vector<uint8_t> mem = std::vector<uint8_t>(g_memSize, 0);
		std::vector<std::pair<uint16_t, uint32_t>> controlWrites;

		uint8_t read8(const uint32_t _a) override { return mem[_a & (g_memSize - 1)]; }
		uint16_t read16(const uint32_t _a) override { return static_cast<uint16_t>((read8(_a) << 8) | read8(_a + 1)); }
		uint32_t read32(const uint32_t _a) override { return (static_cast<uint32_t>(read16(_a)) << 16) | read16(_a + 2); }
		void write8(const uint32_t _a, const uint8_t _v) override { mem[_a & (g_memSize - 1)] = _v; }
		void write16(const uint32_t _a, const uint16_t _v) override { write8(_a, static_cast<uint8_t>(_v >> 8)); write8(_a + 1, static_cast<uint8_t>(_v)); }
		void write32(const uint32_t _a, const uint32_t _v) override { write16(_a, static_cast<uint16_t>(_v >> 16)); write16(_a + 2, static_cast<uint16_t>(_v)); }
		uint8_t interruptAcknowledge(const uint8_t _level) override { return static_cast<uint8_t>(24 + _level); }
		void writeControlRegister(const uint16_t _reg, const uint32_t _v) override { controlWrites.emplace_back(_reg, _v); }
	};

	std::mt19937 g_rng(12345);

	uint32_t rnd(const uint32_t _n) { return std::uniform_int_distribution<uint32_t>(0, _n - 1)(g_rng); }
	uint32_t rnd32() { return g_rng(); }

	// interesting values more often than uniform ones
	uint32_t rndValue()
	{
		switch(rnd(8))
		{
		case 0: return 0;
		case 1: return 0xffffffff;
		case 2: return 0x80000000;
		case 3: return 0x7fffffff;
		case 4: return rnd(256);
		case 5: return static_cast<uint32_t>(-static_cast<int32_t>(rnd(256)));
		case 6: return rnd32() & 0xffff;
		default: return rnd32();
		}
	}

	struct Instruction
	{
		std::vector<uint16_t> words;
		uint16_t ccrMask = 0x1f;	// condition code bits to compare
		std::string name;
	};

	// an effective address with the extension words it needs; memory modes use A0-A6, which the test points
	// into the middle of the memory, so that no access leaves it
	struct EaChoice
	{
		uint32_t mode;
		uint32_t reg;
		std::vector<uint16_t> ext;
	};

	enum EaSet : uint32_t
	{
		SetDn = 1, SetAn = 2, SetInd = 4, SetPost = 8, SetPre = 16, SetD16 = 32, SetIdx = 64, SetAbsW = 128, SetAbsL = 256,
		SetPcD16 = 512, SetPcIdx = 1024, SetImm = 2048,
		SetMemAlt = SetInd | SetPost | SetPre | SetD16 | SetIdx | SetAbsW | SetAbsL,
		SetDataAlt = SetDn | SetMemAlt,
		SetData = SetDataAlt | SetPcD16 | SetPcIdx | SetImm,
		SetAll = SetData | SetAn,
	};

	EaChoice makeEa(const uint32_t _allowed, const uint32_t _size)
	{
		for(;;)
		{
			const uint32_t pick = 1u << rnd(12);
			if(!(pick & _allowed))
				continue;

			const auto an = rnd(7);	// never A7 in memory modes
			switch(pick)
			{
			case SetDn: return {0, rnd(8), {}};
			case SetAn: return {1, rnd(8), {}};
			case SetInd: return {2, an, {}};
			case SetPost: return {3, an, {}};
			case SetPre: return {4, an, {}};
			case SetD16: return {5, an, {static_cast<uint16_t>(rnd(0x400) - 0x200)}};
			case SetIdx:
				{
					// long index register, scale 1, 2 or 4, brief format; the index register holds a small value
					const uint16_t ext = static_cast<uint16_t>((rnd(2) << 15) | (rnd(7) << 12) | 0x0800 | (rnd(3) << 9) | rnd(256));
					return {6, an, {ext}};
				}
			case SetAbsW: return {7, 0, {static_cast<uint16_t>(0x4000 + rnd(0x1000) * 2)}};
			case SetAbsL: return {7, 1, {0x0000, static_cast<uint16_t>(0x6000 + rnd(0x1000) * 2)}};
			case SetPcD16: return {7, 2, {static_cast<uint16_t>(0x100 + rnd(0x200))}};
			case SetPcIdx:
				{
					const uint16_t ext = static_cast<uint16_t>((rnd(2) << 15) | (rnd(7) << 12) | 0x0800 | (rnd(3) << 9) | rnd(128));
					return {7, 3, {ext}};
				}
			case SetImm:
			default:
				if(_size == 4)
				{
					const uint32_t v = rndValue();
					return {7, 4, {static_cast<uint16_t>(v >> 16), static_cast<uint16_t>(v)}};
				}
				return {7, 4, {static_cast<uint16_t>(_size == 1 ? rnd(256) : rndValue() & 0xffff)}};
			}
		}
	}

	uint16_t eaBits(const EaChoice& _ea) { return static_cast<uint16_t>((_ea.mode << 3) | _ea.reg); }

	Instruction gen()
	{
		Instruction ins;
		auto& w = ins.words;
		auto addExt = [&](const EaChoice& _ea) { w.insert(w.end(), _ea.ext.begin(), _ea.ext.end()); };

		const uint32_t dn = rnd(8);
		const uint32_t an = rnd(7);

		switch(rnd(28))
		{
		case 0:		// ADD/SUB/AND/OR/CMP.L <ea>,Dn
			{
				static constexpr uint16_t ops[] = {0xd080, 0x9080, 0xc080, 0x8080, 0xb080};
				const auto i = rnd(5);
				const auto ea = makeEa(i >= 2 && i <= 3 ? SetData : SetAll, 4);
				w.push_back(static_cast<uint16_t>(ops[i] | (dn << 9) | eaBits(ea)));
				addExt(ea);
				ins.name = "alu <ea>,Dn";
			}
			break;
		case 1:		// ADD/SUB/AND/OR/EOR.L Dn,<ea>
			{
				static constexpr uint16_t ops[] = {0xd180, 0x9180, 0xc180, 0x8180, 0xb180};
				const auto i = rnd(5);
				const auto ea = makeEa(i == 4 ? SetDataAlt : SetMemAlt, 4);
				w.push_back(static_cast<uint16_t>(ops[i] | (dn << 9) | eaBits(ea)));
				addExt(ea);
				ins.name = "alu Dn,<ea>";
			}
			break;
		case 2:		// ORI/ANDI/SUBI/ADDI/EORI/CMPI.L #imm,Dn
			{
				static constexpr uint16_t ops[] = {0x0080, 0x0280, 0x0480, 0x0680, 0x0a80, 0x0c80};
				const auto v = rndValue();
				w = {static_cast<uint16_t>(ops[rnd(6)] | dn), static_cast<uint16_t>(v >> 16), static_cast<uint16_t>(v)};
				ins.name = "imm";
			}
			break;
		case 3:		// ADDQ/SUBQ.L #q,<ea>
			{
				const auto ea = makeEa(SetDataAlt | SetAn, 4);
				w.push_back(static_cast<uint16_t>(0x5080 | (rnd(8) << 9) | (rnd(2) << 8) | eaBits(ea)));
				addExt(ea);
				ins.name = "addq/subq";
			}
			break;
		case 4:		// MOVE.B/W/L
			{
				const auto sz = rnd(3);
				static constexpr uint16_t lines[] = {0x1000, 0x3000, 0x2000};
				static constexpr uint32_t sizes[] = {1, 2, 4};
				const auto src = makeEa(sz == 0 ? SetData : SetAll, sizes[sz]);
				const auto dst = makeEa(SetDataAlt, sizes[sz]);
				w.push_back(static_cast<uint16_t>(lines[sz] | (dst.reg << 9) | (dst.mode << 6) | eaBits(src)));
				addExt(src);
				addExt(dst);
				ins.name = "move";
			}
			break;
		case 5:		// MOVEA.W/L
			{
				const bool word = rnd(2) != 0;
				const auto src = makeEa(SetAll, word ? 2 : 4);
				w.push_back(static_cast<uint16_t>((word ? 0x3040 : 0x2040) | (an << 9) | eaBits(src)));
				addExt(src);
				ins.name = "movea";
			}
			break;
		case 6:		// MOVEQ
			w.push_back(static_cast<uint16_t>(0x7000 | (dn << 9) | rnd(256)));
			ins.name = "moveq";
			break;
		case 7:		// CLR.B/W/L
			{
				const auto sz = rnd(3);
				const auto ea = makeEa(SetDataAlt, 1u << sz);
				w.push_back(static_cast<uint16_t>(0x4200 | (sz << 6) | eaBits(ea)));
				addExt(ea);
				ins.name = "clr";
			}
			break;
		case 8:		// TST.B/W/L
			{
				const auto sz = rnd(3);
				const auto ea = makeEa(sz == 0 ? SetData : SetAll, 1u << sz);
				w.push_back(static_cast<uint16_t>(0x4a00 | (sz << 6) | eaBits(ea)));
				addExt(ea);
				ins.name = "tst";
			}
			break;
		case 9:		// NEG/NEGX/NOT.L Dn, EXT.W/L, EXTB.L, SWAP
			{
				static constexpr uint16_t ops[] = {0x4480, 0x4080, 0x4680, 0x4880, 0x48c0, 0x49c0, 0x4840};
				w.push_back(static_cast<uint16_t>(ops[rnd(7)] | dn));
				ins.name = "unary";
			}
			break;
		case 10:	// ADDX/SUBX.L Dy,Dx
			w.push_back(static_cast<uint16_t>((rnd(2) ? 0xd180 : 0x9180) | (dn << 9) | rnd(8)));
			ins.name = "addx/subx";
			break;
		case 11:	// LSL/LSR/ASR.L #/Dy, and ASL with V masked
			{
				const auto type = rnd(4);	// 0 ASR 1 LSR 2 LSL 3 ASL
				const bool left = type >= 2;
				const bool logical = type == 1 || type == 2;
				const bool reg = rnd(2) != 0;
				w.push_back(static_cast<uint16_t>(0xe080 | (rnd(8) << 9) | (left ? 0x100 : 0) | (reg ? 0x20 : 0) | (logical ? 0x08 : 0) | dn));
				if(type == 3)
					ins.ccrMask = 0x1d;
				ins.name = "shift";
			}
			break;
		case 12:	// BTST/BCHG/BCLR/BSET Dn,<ea> and #,<ea>
			{
				const auto type = rnd(4);
				const bool imm = rnd(2) != 0;
				const uint32_t allowed = imm ? (SetDn | SetInd | SetPost | SetPre | SetD16 | (type == 0 ? SetPcD16 : 0)) : (type == 0 ? (SetDataAlt | SetPcD16 | SetPcIdx) : SetDataAlt);
				const auto ea = makeEa(allowed, 1);
				if(imm)
				{
					w.push_back(static_cast<uint16_t>(0x0800 | (type << 6) | eaBits(ea)));
					w.push_back(static_cast<uint16_t>(rnd(256)));
				}
				else
				{
					w.push_back(static_cast<uint16_t>(0x0100 | (dn << 9) | (type << 6) | eaBits(ea)));
				}
				addExt(ea);
				ins.name = "bitop";
			}
			break;
		case 13:	// Scc Dn
			w.push_back(static_cast<uint16_t>(0x50c0 | (rnd(16) << 8) | dn));
			ins.name = "scc";
			break;
		case 14:	// Bcc/BRA with 8 and 16 bit displacements
			{
				const auto cc = rnd(16);
				if(cc == 1)
					break;
				if(rnd(2))
				{
					w.push_back(static_cast<uint16_t>(0x6000 | (cc << 8) | ((rnd(60) + 2) & 0xfe)));
				}
				else
				{
					w.push_back(static_cast<uint16_t>(0x6000 | (cc << 8)));
					w.push_back(static_cast<uint16_t>((rnd(0x400) - 0x200) & 0xfffe));
				}
				ins.name = "bcc";
			}
			break;
		case 15:	// LEA/PEA
			{
				const auto ea = makeEa(SetInd | SetD16 | SetIdx | SetAbsW | SetAbsL | SetPcD16 | SetPcIdx, 4);
				w.push_back(static_cast<uint16_t>((rnd(2) ? 0x41c0 | (an << 9) : 0x4840) | eaBits(ea)));
				addExt(ea);
				ins.name = "lea/pea";
			}
			break;
		case 16:	// MULU/MULS.W
			{
				const auto ea = makeEa(SetData, 2);
				w.push_back(static_cast<uint16_t>((rnd(2) ? 0xc1c0 : 0xc0c0) | (dn << 9) | eaBits(ea)));
				addExt(ea);
				ins.name = "mul.w";
			}
			break;
		case 17:	// MULU/MULS.L, V differs (the ColdFire clears it)
			{
				const auto ea = makeEa(SetDn | SetInd | SetPost | SetPre | SetD16, 4);
				w.push_back(static_cast<uint16_t>(0x4c00 | eaBits(ea)));
				w.push_back(static_cast<uint16_t>((dn << 12) | (rnd(2) << 11)));
				addExt(ea);
				ins.ccrMask = 0x1d;
				ins.name = "mul.l";
			}
			break;
		case 18:	// DIVU/DIVS.W, the divisor is never zero (see setup), N and Z are undefined on overflow
			{
				const auto ea = makeEa(SetDn | SetImm, 2);
				w.push_back(static_cast<uint16_t>((rnd(2) ? 0x81c0 : 0x80c0) | (dn << 9) | eaBits(ea)));
				if(ea.mode == 7 && ea.ext[0] == 0)
					w.push_back(1);
				else
					addExt(ea);
				ins.name = "div.w";
			}
			break;
		case 19:	// DIVU/DIVS.L <ea>,Dq (quotient form only; REMx.L differs from DIVxL.L)
			{
				const auto ea = makeEa(SetDn, 4);
				w.push_back(static_cast<uint16_t>(0x4c40 | eaBits(ea)));
				w.push_back(static_cast<uint16_t>((dn << 12) | (rnd(2) << 11) | dn));
				ins.name = "div.l";
			}
			break;
		case 20:	// MOVEM.L to and from (An), (d16,An)
			{
				const bool toMem = rnd(2) != 0;
				const auto ea = makeEa(SetInd | SetD16, 4);
				// A7 is not transferred: Musashi keeps separate stack pointers
				w.push_back(static_cast<uint16_t>((toMem ? 0x48c0 : 0x4cc0) | eaBits(ea)));
				w.push_back(static_cast<uint16_t>(rnd(0x8000) | 1));
				addExt(ea);
				ins.name = "movem";
			}
			break;
		case 21:	// LINK.W / UNLK with A0-A6
			if(rnd(2))
			{
				w.push_back(static_cast<uint16_t>(0x4e50 | an));
				w.push_back(static_cast<uint16_t>((rnd(0x100) - 0x80) & 0xfffe));
			}
			else
			{
				w.push_back(static_cast<uint16_t>(0x4e58 | an));
			}
			ins.name = "link/unlk";
			break;
		case 22:	// MOVE to/from CCR, MOVE from SR
			{
				const auto k = rnd(3);
				if(k == 0)
				{
					w.push_back(static_cast<uint16_t>(0x42c0 | dn));
				}
				else if(k == 1)
				{
					w.push_back(static_cast<uint16_t>(0x40c0 | dn));
				}
				else
				{
					const auto ea = makeEa(SetDn | SetImm, 2);
					w.push_back(static_cast<uint16_t>(0x44c0 | eaBits(ea)));
					addExt(ea);
				}
				ins.name = "ccr/sr";
			}
			break;
		case 23:	// ADDA/SUBA/CMPA.L
			{
				static constexpr uint16_t ops[] = {0xd1c0, 0x91c0, 0xb1c0};
				const auto ea = makeEa(SetAll, 4);
				w.push_back(static_cast<uint16_t>(ops[rnd(3)] | (an << 9) | eaBits(ea)));
				addExt(ea);
				ins.name = "adda/suba/cmpa";
			}
			break;
		case 24:	// JMP/JSR to even targets, an odd one is an address error on the ColdFire only
			{
				auto ea = makeEa(SetAbsW | SetAbsL | SetPcD16, 4);
				ea.ext.back() &= 0xfffe;
				w.push_back(static_cast<uint16_t>((rnd(2) ? 0x4ec0 : 0x4e80) | eaBits(ea)));
				addExt(ea);
				ins.name = "jmp/jsr";
			}
			break;
		case 25:	// BSR, RTS
			if(rnd(2))
				w.push_back(static_cast<uint16_t>(0x6100 | ((rnd(60) + 2) & 0xfe)));
			else
				w.push_back(0x4e75);
			ins.name = "bsr/rts";
			break;
		case 26:	// TRAPF variants, NOP
			{
				static constexpr uint16_t ops[] = {0x51fc, 0x51fa, 0x51fb, 0x4e71};
				const auto k = rnd(4);
				w.push_back(ops[k]);
				if(k == 1)
					w.push_back(static_cast<uint16_t>(rnd32()));
				if(k == 2)
				{
					w.push_back(static_cast<uint16_t>(rnd32()));
					w.push_back(static_cast<uint16_t>(rnd32()));
				}
				ins.name = "trapf/nop";
			}
			break;
		default:	// MOVE.W to SR with S set, stays in supervisor mode and keeps the mask
			{
				const auto v = static_cast<uint16_t>(0x2700 | rnd(0x20));
				w = {0x46fc, v};
				ins.name = "move to sr";
			}
			break;
		}

		if(w.empty())
			return gen();
		return ins;
	}

	int g_failures = 0;
	std::map<std::string, int> g_failuresPerName;

	void fail(const char* _what, const Instruction& _ins, const uint32_t _a, const uint32_t _b)
	{
		++g_failures;
		if(++g_failuresPerName[_ins.name] > 4)
			return;
		printf("MISMATCH %s: coldfire %08x musashi %08x in '%s':", _what, _a, _b, _ins.name.c_str());
		for(const auto w : _ins.words)
			printf(" %04x", w);
		printf("\n");
	}

	void differentialTest(const uint32_t _count)
	{
		RefCpu ref;
		RamBus bus;
		coldfire::Cpu cpu(bus);

		auto* core = ref.getCpuState();

		for(uint32_t n = 0; n < _count; ++n)
		{
			const Instruction ins = gen();

			// same memory for both
			for(uint32_t i = 0; i < g_memSize; i += 4)
			{
				const uint32_t v = rnd32();
				for(uint32_t b = 0; b < 4; ++b)
					bus.mem[i + b] = static_cast<uint8_t>(v >> (24 - b * 8));
			}
			for(size_t i = 0; i < ins.words.size(); ++i)
			{
				bus.mem[g_codeAddr + i * 2] = static_cast<uint8_t>(ins.words[i] >> 8);
				bus.mem[g_codeAddr + i * 2 + 1] = static_cast<uint8_t>(ins.words[i]);
			}
			// a return address for RTS
			const uint32_t sp = 0xe000 + rnd(0x100) * 4;
			bus.mem[sp] = 0; bus.mem[sp + 1] = 0; bus.mem[sp + 2] = 0x20; bus.mem[sp + 3] = static_cast<uint8_t>(rnd(64) * 2);
			ref.mem = bus.mem;

			const uint16_t sr = static_cast<uint16_t>(0x2700 | rnd(0x20));
			m68k_set_reg(core, M68K_REG_SR, sr);
			cpu.setSR(sr);

			for(uint32_t r = 0; r < 8; ++r)
			{
				uint32_t d = rndValue();
				// DIV.W/L divisor registers must not be zero
				if(!d)
					d = 1;
				m68k_set_reg(core, static_cast<m68k_register_t>(M68K_REG_D0 + r), d);
				cpu.setD(r, d);
			}
			for(uint32_t r = 0; r < 7; ++r)
			{
				// memory modes stay within the memory: address registers point to its middle
				const uint32_t a = 0x8000 + rnd(0x2000) * 2 + rnd(2);
				m68k_set_reg(core, static_cast<m68k_register_t>(M68K_REG_A0 + r), a);
				cpu.setA(r, a);
			}
			m68k_set_reg(core, M68K_REG_A7, sp);
			m68k_set_reg(core, M68K_REG_ISP, sp);
			cpu.setA(7, sp);

			// DIV.W divides by the low word only, which must not be zero either
			if(ins.name == "div.w")
			{
				for(uint32_t r = 0; r < 8; ++r)
				{
					uint32_t d = cpu.getD(r);
					if(!(d & 0xffff))
						d |= 1;
					m68k_set_reg(core, static_cast<m68k_register_t>(M68K_REG_D0 + r), d);
					cpu.setD(r, d);
				}
			}

			m68k_set_reg(core, M68K_REG_PC, g_codeAddr);
			cpu.setPC(g_codeAddr);

			// keep index registers small: an indexed EA scales them by up to 4
			for(uint32_t r = 0; r < 8; ++r)
			{
				const uint32_t d = cpu.getD(r);
				if(d > 0x800 && d < 0xfffff800 && (ins.name == "alu <ea>,Dn" || ins.name == "alu Dn,<ea>" || ins.name == "move" || ins.name == "movea" || ins.name == "clr" || ins.name == "tst" || ins.name == "bitop" || ins.name == "lea/pea" || ins.name == "mul.w" || ins.name == "adda/suba/cmpa" || ins.name == "jmp/jsr" || ins.name == "addq/subq"))
				{
					const uint32_t small = rnd(0x400);
					m68k_set_reg(core, static_cast<m68k_register_t>(M68K_REG_D0 + r), small);
					cpu.setD(r, small);
				}
			}

			m68k_execute(core, 1);
			cpu.step();

			for(uint32_t r = 0; r < 8; ++r)
			{
				const auto a = cpu.getD(r);
				const auto b = static_cast<uint32_t>(m68k_get_reg(core, static_cast<m68k_register_t>(M68K_REG_D0 + r)));
				if(a != b)
					fail(("d" + std::to_string(r)).c_str(), ins, a, b);
			}
			for(uint32_t r = 0; r < 8; ++r)
			{
				const auto a = cpu.getA(r);
				const auto b = static_cast<uint32_t>(m68k_get_reg(core, static_cast<m68k_register_t>(M68K_REG_A0 + r)));
				if(a != b)
					fail(("a" + std::to_string(r)).c_str(), ins, a, b);
			}

			const auto pcA = cpu.getPC();
			const auto pcB = static_cast<uint32_t>(m68k_get_reg(core, M68K_REG_PC));
			if(pcA != pcB)
				fail("pc", ins, pcA, pcB);

			const uint32_t srA = cpu.getSR();
			const auto srB = static_cast<uint32_t>(m68k_get_reg(core, M68K_REG_SR));
			// On a DIV overflow the PRMs leave N and Z undefined and clear C, Musashi sets C, and for
			// $80000000 / -1 in DIVS.L it misses the overflow altogether. The registers are still compared.
			uint32_t mask = ins.ccrMask;
			if((ins.name == "div.w" || ins.name == "div.l") && (srA & coldfire::Cpu::SrV))
				mask = coldfire::Cpu::SrX;
			if((srA & mask) != (srB & mask) || (srA & 0xff00) != (srB & 0xff00))
				fail("sr", ins, srA, srB);

			for(uint32_t i = 0; i < g_memSize; ++i)
			{
				if(bus.mem[i] != ref.mem[i])
				{
					fail(("mem " + std::to_string(i)).c_str(), ins, bus.mem[i], ref.mem[i]);
					break;
				}
			}
		}

		printf("differential test: %u instructions, %d mismatches\n", _count, g_failures);
		for(const auto& [name, count] : g_failuresPerName)
			printf("  %s: %d\n", name.c_str(), count);
	}

	void check(const bool _ok, const char* _what)
	{
		if(_ok)
			return;
		++g_failures;
		printf("FAILED: %s\n", _what);
	}

	void coldfireSpecifics()
	{
		RamBus bus;
		coldfire::Cpu cpu(bus);

		auto load = [&](const std::vector<uint16_t>& _code)
		{
			for(size_t i = 0; i < _code.size(); ++i)
				bus.write16(static_cast<uint32_t>(g_codeAddr + i * 2), _code[i]);
			cpu.setPC(g_codeAddr);
			cpu.setSR(0x2700);
		};

		// TRAP #3 with a misaligned stack: format 6 frame 10 bytes below, vector in the format word, SR and PC
		load({0x4e43});
		bus.write32(0x80 + 3 * 4, 0x1000);
		cpu.setVBR(0);
		cpu.setA(7, 0x2002);
		cpu.step();
		check(cpu.getPC() == 0x1000, "trap vector");
		check(cpu.getA(7) == 0x2002 - 10, "frame is 10 bytes below a 2-mod-4 stack");
		check(bus.read16(0x1ff8) == ((6 << 12) | ((32 + 3) << 2)), "format 6 and vector 35");
		check(bus.read16(0x1ffa) == 0x2700, "stacked SR");
		check(bus.read32(0x1ffc) == g_codeAddr + 2, "stacked PC is the next instruction");

		// RTE from there restores SR, PC and the original stack
		bus.write16(0x1000, 0x4e73);
		cpu.step();
		check(cpu.getPC() == g_codeAddr + 2, "rte pc");
		check(cpu.getA(7) == 0x2002, "rte restores the misaligned stack");

		// REMS.L: remainder in Dw, Dx unchanged
		load({0x4c41, 0x0802});	// rems.l d1,d2:d0
		cpu.setD(0, static_cast<uint32_t>(-17));
		cpu.setD(1, 5);
		cpu.setD(2, 0);
		cpu.step();
		check(cpu.getD(2) == static_cast<uint32_t>(-2), "rems remainder");
		check(cpu.getD(0) == static_cast<uint32_t>(-17), "rems keeps the dividend");

		// REMU.L
		load({0x4c41, 0x0002});	// remu.l d1,d2:d0
		cpu.setD(0, 17);
		cpu.setD(1, 5);
		cpu.step();
		check(cpu.getD(2) == 2 && cpu.getD(0) == 17, "remu");

		// ASL clears V where a 68000 would set it
		load({0xe380});			// asl.l #1,d0
		cpu.setD(0, 0x40000000);
		cpu.step();
		check(cpu.getD(0) == 0x80000000 && !(cpu.getSR() & coldfire::Cpu::SrV), "asl V is always cleared");

		// (A7)+ with a byte steps by one
		load({0x101f});			// move.b (a7)+,d0
		cpu.setA(7, 0x3000);
		cpu.step();
		check(cpu.getA(7) == 0x3001, "(a7)+ byte steps by one");

		// divide by zero: vector 5, the stacked PC is the DIVU instruction
		load({0x80c1});			// divu.w d1,d0
		bus.write32(5 * 4, 0x1100);
		cpu.setD(1, 0);
		cpu.setA(7, 0x2000);
		cpu.step();
		check(cpu.getPC() == 0x1100 && bus.read32(0x2000 - 4) == g_codeAddr, "divide by zero");

		// MOVEC to VBR and to a chip register
		load({0x4e7b, 0x0801, 0x4e7b, 0x0c0f});	// movec d0,vbr; movec d0,mbar
		cpu.setD(0, 0x10000001);
		bus.controlWrites.clear();
		cpu.step();
		cpu.step();
		check(cpu.getVBR() == 0x10000000, "movec vbr keeps bits 31-20");
		check(bus.controlWrites.size() == 1 && bus.controlWrites[0].first == 0xc0f && bus.controlWrites[0].second == 0x10000001, "movec mbar");

		// an interrupt stacks the old SR and raises the mask, M is cleared
		load({0x4e71, 0x4e71});
		bus.write32((24 + 5) * 4, 0x1200);
		cpu.setSR(0x3200);
		cpu.setA(7, 0x2000);
		cpu.setInterruptLevel(5);
		cpu.step();
		check(cpu.getPC() == 0x1200, "interrupt vector");
		check((cpu.getSR() & 0x1700) == 0x0500, "mask raised, M cleared");
		check(bus.read16(0x2000 - 6) == 0x3200, "old SR stacked");
		cpu.setInterruptLevel(0);

		printf("coldfire specific tests done, %d failures in total\n", g_failures);
	}
}

int main()
{
	differentialTest(200000);
	coldfireSpecifics();
	return g_failures ? 1 : 0;
}
