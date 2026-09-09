#pragma once

#include "lsp_common.h"

namespace lspLib
{
	// The program store: raw words as the host wrote them, the decoded
	// instruction cache, and the two live tables the engines read at run time.
	//
	// A host write is classified by what it changes:
	//
	//   cram        only the coefficient byte of a MAC or a non-jump special
	//               moved, without crossing zero on an accumulating op.
	//               coefs[] is patched; no re-decode, no re-JIT.
	//   eram        only ERAM address bits moved (a delay-time edit). The
	//               affected accesses' eramAddr[] entries are patched; no
	//               re-decode, no re-JIT.
	//   structural  anything else: opcode, write control, memOffs, a MUL flag
	//               byte, a jump target, an access appearing or disappearing.
	//               cacheProgram() re-decodes and a JIT recompiles.
	class LSPProgram
	{
	public:
		enum Taint : uint8_t
		{
			TaintCram       = 1,
			TaintEram       = 2,
			TaintStructural = 4,
		};

		int32_t  words[ProgramWords] = {0};
		LSPInstr instr[ProgramWords] = {};
		int32_t  coefs[ProgramWords] = {0};		// cc, sign-extended
		uint16_t eramAddr[ProgramWords] = {0};	// ERAM base address of the access at this slot

		// Set by cacheProgram(): a JIT needs the per-sample budget counter only
		// when execution can revisit a slot.
		bool     hasJump = false;
		bool     hasBackwardJump = false;

		bool hasProgram = false;

		// Kept public with the tables above: the JIT takes offsetof() on this
		// class, which needs standard layout.
		uint8_t  taintBits = TaintStructural;

		void clear()
		{
			std::memset(words, 0, sizeof(words));
			std::memset(coefs, 0, sizeof(coefs));
			std::memset(eramAddr, 0, sizeof(eramAddr));
			for(auto& i : instr)
				i = LSPInstr{};
			hasJump = hasBackwardJump = false;
			hasProgram = false;
			taintBits = TaintStructural;
		}

		bool     tainted() const { return (taintBits & TaintStructural) != 0; }
		uint8_t  taint() const { return taintBits; }
		void     clearTaint(const uint8_t _bits) { taintBits &= static_cast<uint8_t>(~_bits); }

		int32_t read(const uint32_t _pc) const { return words[_pc]; }

		void write(const uint32_t _pc, const int32_t _word)
		{
			const int32_t word = _word & 0xffffff;
			const int32_t old = words[_pc];
			if(old == word)
				return;
			words[_pc] = word;

			// Skip boundaries and MUL flag bytes are structural; so is a jump
			// target, which is the coefficient byte of the jump special.
			const bool ccOnly = old != 0 && word != 0 && ((old ^ word) & 0xffff00) == 0;
			if(ccOnly && !tainted())
			{
				const LSPInstr& i = instr[_pc];
				const bool isJump = i.op == Op::Special && (i.slot == SlotJumpIfNegative ||
					i.slot == SlotJumpIfNonNegative || i.slot == SlotJumpAlways);
				// Crossing zero on an accumulating op flips zeroCoef, which
				// changes the register plan: structural.
				const bool crossesZero = (i.writesAcc || i.zeroCoef) && !i.replace &&
					((old & 0xff) == 0) != ((word & 0xff) == 0);
				if(!crossesZero && (i.op == Op::Mac || (i.op == Op::Special && !isJump)))
				{
					instr[_pc].cc = static_cast<int8_t>(word);
					coefs[_pc] = static_cast<int8_t>(word);
					taintBits |= TaintCram;
					return;
				}
			}

			const bool eramBitsOnly = ((old ^ word) & ~0x070000) == 0;
			if(eramBitsOnly && !tainted() && patchEramAddresses(_pc))
			{
				taintBits |= TaintEram;
				return;
			}

			taintBits |= TaintStructural;
			hasProgram = true;
		}

		void cacheProgram()
		{
			for(uint32_t pc = 0; pc < ProgramWords; ++pc)
			{
				instr[pc] = decode(words[pc]);
				coefs[pc] = instr[pc].cc;
			}

			hasJump = hasBackwardJump = false;
			for(uint32_t pc = 1; pc < ProgramWords; ++pc)
			{
				LSPInstr& o = instr[pc];
				const LSPInstr& prev = instr[pc - 1];
				o.prevMem = prev.memOffs;

				if(prev.op != Op::Special)
					continue;
				switch(prev.slot)
				{
				case SlotJumpIfNegative:    o.jump = Jump::IfNegative;    break;
				case SlotJumpIfNonNegative: o.jump = Jump::IfNonNegative; break;
				case SlotJumpAlways:        o.jump = Jump::Always;        break;
				default: continue;
				}
				o.jumpDest = static_cast<uint16_t>((static_cast<uint8_t>(prev.cc) << 1) - 0x80);

				// Programs end in a one-instruction self-loop; treating it as
				// a jump would burn the whole slot budget every sample.
				if(o.jumpDest == pc - 1)
				{
					o.jump = Jump::None;
					o.jumpDest = 0;
					continue;
				}
				hasJump = true;
				if(o.jumpDest <= pc)
					hasBackwardJump = true;
				if(o.jumpDest < ProgramWords)
					instr[o.jumpDest].isJumpTarget = true;
			}

			for(uint32_t pc = 0; pc < ProgramWords; ++pc)
			{
				const EramAccess a = decodeEram(pc);
				instr[pc].eramRead = a.read;
				instr[pc].eramWrite = a.write;
				instr[pc].eramSecondTap = a.secondTap;
				eramAddr[pc] = a.base;
			}

			assignRegs();
			taintBits = 0;
		}

	private:
		struct EramAccess
		{
			bool read = false;
			bool write = false;
			bool secondTap = false;
			uint16_t base = 0;
		};

		static LSPInstr decode(const int32_t _word)
		{
			LSPInstr o;
			if(_word == 0)
				return o;

			o.ii = static_cast<uint8_t>(_word >> 16);
			o.rr = static_cast<uint8_t>(_word >> 8);
			o.cc = static_cast<int8_t>(_word);

			const uint8_t opcode = o.ii & 0xe0;
			o.memOffs = o.rr & 0x7f;
			o.scaler = (o.rr & 0x80) ? 5 : 7;
			switch(o.ii & 0x18)
			{
			case 0x08: o.src = Src::ASat; break;
			case 0x10: o.src = Src::BSat; break;
			case 0x18: o.src = Src::ARaw; break;
			default: break;
			}
			if(o.memOffs >= 1 && o.memOffs <= 4)
				o.immShift = static_cast<uint8_t>(2 + o.memOffs * 5);

			if(opcode == 0x80)
			{
				o.op = Op::Mul;
				o.mulLower  = (o.cc & 0x40) != 0;
				o.accB      = (o.cc & 0x10) != 0;
				o.mulNegate = (o.cc & 0x04) != 0;
				o.replace   = (o.cc & 0x08) != 0 && !o.mulLower;
				o.mulCoef2  = (o.cc & 0x02) != 0;
				o.mulZero   = o.cc == 0;
				o.writesAcc = true;
			}
			else if(opcode == 0xc0 || opcode == 0xe0)
			{
				o.op = Op::Special;
				o.accB    = (o.ii & 0x20) != 0;
				o.replace = (o.rr & 0x20) != 0;
				o.slot    = o.rr & 0x1f;
				o.imm50d0 = o.src == Src::None && o.slot == SlotEramWriteLatch;
				switch(o.slot)
				{
				case SlotJumpIfNegative:
				case SlotJumpIfNonNegative:
				case SlotJumpAlways:
				case SlotEramWriteLatch:
				case SlotMulCoef1:
				case SlotMulCoef2:
					break;
				case SlotEramTapAndCoef1:
					break;
				case SlotAudioOut:
				case SlotEramRead0: case SlotEramRead0 + 1: case SlotEramRead0 + 2: case SlotEramRead0 + 3:
				case SlotAudioIn:
					o.writesAcc = true;
					break;
				default:
					break;
				}
				if(o.imm50d0)
				{
					o.writesAcc = true;
				}
			}
			else if(opcode != 0x80)
			{
				o.op = Op::Mac;
				o.abs     = opcode == 0xa0;
				o.replace = (o.ii & 0x20) != 0 || o.abs;
				o.accB    = (o.ii & 0x40) != 0;
				o.writesAcc = true;
			}

			// An op that accumulates a zero coefficient leaves the accumulator
			// alone (MUL excepted: its zero form still saturates the running
			// value). The ERAM address-run filler words are such MACs, a fifth
			// of every program, so they drop out of the pipeline plan and a JIT
			// emits only their store.
			if(o.writesAcc && o.op != Op::Mul && !o.replace && o.cc == 0)
			{
				o.zeroCoef = true;
				o.writesAcc = false;
			}
			return o;
		}

		// An access is addressed by the six instructions before it, each
		// contributing three bits through its ext-RAM control field — eight
		// slots back for a write, twelve for a read.
		EramAccess decodeEram(const uint32_t _pc) const
		{
			EramAccess a;
			const LSPInstr& o = instr[_pc];
			if(o.op != Op::Special || o.src == Src::None)
				return a;
			a.read  = o.slot >= SlotEramRead0 && o.slot <= SlotEramRead0 + 3;
			a.write = o.slot == SlotEramWriteLatch;
			if(!a.read && !a.write)
				return a;

			const int start = static_cast<int>(_pc) - (a.write ? 8 : 12);
			if(start < 0)
				return a;

			const auto ctrl = [&](const int _i) { return static_cast<uint8_t>((words[start + _i] >> 16) & 0x07); };
			a.secondTap = (ctrl(0) & 0x06) == 0x04;
			uint32_t base = 0;
			for(int i = 1; i <= 6; ++i)
			{
				uint16_t incr = static_cast<uint16_t>(ctrl(i) << ((i - 1) * 3));
				if(a.secondTap)
					incr = (ctrl(i) == 0x02 && i == 1) ? 1 : 0;
				if(i < 6 || (ctrl(i) & 1))
					base += incr;
			}
			a.base = static_cast<uint16_t>(base);
			return a;
		}

		// A write that moved only ext-RAM control bits at _pc affects the
		// accesses in the twelve slots after it. Live-patch their addresses if
		// nothing else about them changed.
		bool patchEramAddresses(const uint32_t _pc)
		{
			EramAccess fresh[13];
			const uint32_t end = _pc + 12 < ProgramWords ? _pc + 12 : ProgramWords - 1;
			for(uint32_t pc = _pc; pc <= end; ++pc)
			{
				const LSPInstr& o = instr[pc];
				const EramAccess a = decodeEram(pc);
				if(a.read != o.eramRead || a.write != o.eramWrite || a.secondTap != o.eramSecondTap)
					return false;
				fresh[pc - _pc] = a;
			}
			for(uint32_t pc = _pc; pc <= end; ++pc)
				eramAddr[pc] = fresh[pc - _pc].base;
			return true;
		}

		// Flatten the three-deep accumulator pipeline onto three registers per
		// accumulator. Within straight-line code the pipeline is simulated
		// symbolically: a writer takes any register not holding v(t-2) or
		// v(t-1), and the state shifts. At every jump target the state is
		// canonical (v(t-3), v(t-2), v(t-1) in registers 0,1,2 / 3,4,5), which
		// is also the form the runtime carries between samples; a JIT emits the
		// moves that canonicalise on the fall-through into a target and on the
		// taken path of a jump.
		void assignRegs()
		{
			static constexpr uint8_t kCanonical[6] = {0, 1, 2, 3, 4, 5};
			uint8_t st[6];
			std::memcpy(st, kCanonical, sizeof(st));

			for(uint32_t pc = 0; pc < ProgramWords; ++pc)
			{
				LSPInstr& o = instr[pc];
				if(pc > 0 && o.isJumpTarget)
					std::memcpy(st, kCanonical, sizeof(st));
				std::memcpy(o.stateIn, st, sizeof(st));

				for(int base = 0; base < 6; base += 3)
				{
					const bool writes = o.writesAcc && (o.accB == (base == 3));
					uint8_t dest = st[base + 2];
					if(writes)
					{
						for(uint8_t r = static_cast<uint8_t>(base); r < base + 3; ++r)
						{
							if(r != st[base + 1] && r != st[base + 2])
							{
								dest = r;
								break;
							}
						}
						o.destReg = dest;
					}
					st[base] = st[base + 1];
					st[base + 1] = st[base + 2];
					st[base + 2] = dest;
				}
				std::memcpy(o.stateOut, st, sizeof(st));
			}
		}
	};
}
