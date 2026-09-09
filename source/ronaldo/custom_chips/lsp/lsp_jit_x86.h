#pragma once

#include <cstddef>
#include <map>
#include <vector>

#include <asmjit/asmjit.h>
#include <asmjit/x86.h>

#include "lsp_program.h"

namespace lspLib
{
	// x86-64 backend, the mirror of lsp_jit_arm64.h: the accumulator pipeline
	// is flattened onto six callee-saved registers by LSPProgram::assignRegs,
	// the runtime and program are reached through two base registers, the ERAM
	// latches and the pending-jump flag stay in the runtime image (memory
	// operands).
	class LSPJIT
	{
	public:
		LSPJIT(const LSPProgram& _program, LSPRuntime& _runtime) : m_program(_program), m_rt(_runtime) {}
		~LSPJIT() { release(); }

		LSPJIT(const LSPJIT&) = delete;
		LSPJIT& operator=(const LSPJIT&) = delete;

		bool ready() const { return m_run != nullptr; }

		void runProgram()
		{
			m_run();
			m_rt.endOfPass();
		}

		void release()
		{
			if(!m_run)
				return;
			m_jitRuntime.release(m_run);
			m_run = nullptr;
		}

		bool compile(const LSPInstr* _snapshot)
		{
			namespace x86 = asmjit::x86;

			release();
			m_snap = _snapshot;

			asmjit::CodeHolder code;
			code.init(m_jitRuntime.environment());
			x86::Builder a(&code);
			m_asm = &a;

			m_needCounter = false;
			for(uint32_t pc = 0; pc < ProgramWords; ++pc)
				if(m_snap[pc].jump != Jump::None && m_snap[pc].jumpDest <= pc)
					m_needCounter = true;

			m_labels.assign(ProgramWords, asmjit::Label());
			for(uint32_t pc = 0; pc < ProgramWords; ++pc)
				if(m_snap[pc].isJumpTarget)
					m_labels[pc] = a.newLabel();
			m_epilogue = a.newLabel();
			m_exits.clear();

			// Prologue: every callee-saved register of either ABI.
			for(const auto& r : { x86::rbx, x86::rbp, x86::rsi, x86::rdi, x86::r12, x86::r13, x86::r14, x86::r15 })
				a.push(r);

			a.mov(regRt, reinterpret_cast<uint64_t>(&m_rt));
			a.mov(regProg, reinterpret_cast<uint64_t>(&m_program));
			a.movzx(regBufferPos.r32(), x86::byte_ptr(regRt, static_cast<int32_t>(offsetof(LSPRuntime, bufferPos))));
			a.movzx(regEramPos.r32(), x86::word_ptr(regRt, static_cast<int32_t>(offsetof(LSPRuntime, eramPos))));
			for(int k = 0; k < 6; ++k)
				a.movsxd(acc(k), rt(offsetof(LSPRuntime, accs) + k * 4));
			a.mov(t0.r32(), rt(offsetof(LSPRuntime, audioInR)));
			a.mov(rt(offsetof(LSPRuntime, audioIn)), t0.r32());
			if(m_needCounter)
				a.mov(regBudget, ProgramWords + 1);

			for(uint32_t pc = 0; pc < ProgramWords; ++pc)
				emitSlot(pc);

			emitAccStores(m_snap[ProgramWords - 1].stateOut);
			a.jmp(m_epilogue);

			for(const auto& e : m_exits)
			{
				a.bind(e.second);
				uint8_t st[6];
				for(int k = 0; k < 6; ++k)
					st[k] = static_cast<uint8_t>(e.first >> (k * 8));
				emitAccStores(st);
				a.jmp(m_epilogue);
			}

			a.bind(m_epilogue);
			for(const auto& r : { x86::r15, x86::r14, x86::r13, x86::r12, x86::rdi, x86::rsi, x86::rbp, x86::rbx })
				a.pop(r);
			a.ret();

			if(a.finalize() != asmjit::kErrorOk)
				return false;
			return m_jitRuntime.add(&m_run, &code) == asmjit::kErrorOk;
		}

	private:
		using Run = void(*)();

		static constexpr auto regRt        = asmjit::x86::rbx;
		static constexpr auto regProg      = asmjit::x86::r12;
		static constexpr auto regBufferPos = asmjit::x86::r13;
		static constexpr auto regEramPos   = asmjit::x86::r14;
		static constexpr auto regBudget    = asmjit::x86::rcx;
		static constexpr auto t0 = asmjit::x86::rax;
		static constexpr auto t1 = asmjit::x86::rdx;
		static constexpr auto t2 = asmjit::x86::r10;
		static constexpr auto t3 = asmjit::x86::r11;
		static constexpr int kMoveTemp = 6;

		static asmjit::x86::Gpq acc(const int _r)
		{
			namespace x86 = asmjit::x86;
			static constexpr x86::Gpq regs[6] = { x86::r15, x86::rbp, x86::rsi, x86::rdi, x86::r8, x86::r9 };
			return regs[_r];
		}

		static asmjit::x86::Mem rt(const size_t _offset)
		{
			return asmjit::x86::dword_ptr(regRt, static_cast<int32_t>(_offset));
		}

		asmjit::x86::Mem coef(const uint32_t _pc) const
		{
			return asmjit::x86::dword_ptr(regProg, static_cast<int32_t>(offsetof(LSPProgram, coefs) + _pc * 4));
		}

		// t2 = (bufferPos + memOffs) & 0x7f
		void emitRingAddr(const uint8_t _memOffs)
		{
			m_asm->mov(t2.r32(), regBufferPos.r32());
			m_asm->add(t2.r32(), _memOffs);
			m_asm->and_(t2.r32(), DataRingMask);
		}

		asmjit::x86::Mem ringAt(const asmjit::x86::Gpq& _index) const
		{
			return asmjit::x86::dword_ptr(regRt, _index, 2, static_cast<int32_t>(offsetof(LSPRuntime, iram)));
		}

		void emitRingStore(const asmjit::x86::Gpq& _value, const uint8_t _memOffs)
		{
			emitRingAddr(_memOffs);
			m_asm->mov(ringAt(t2), _value.r32());
		}

		void emitRingLoad(const asmjit::x86::Gpq& _dst, const uint8_t _memOffs)
		{
			emitRingAddr(_memOffs);
			m_asm->movsxd(_dst, ringAt(t2));
		}

		// _dst = clamp24(_src), clobbers t3
		void emitSat24(const asmjit::x86::Gpq& _dst, const asmjit::x86::Gpq& _src)
		{
			if(_dst != _src)
				m_asm->mov(_dst, _src);
			m_asm->mov(t3, -0x800000);
			m_asm->cmp(_dst, t3);
			m_asm->cmovl(_dst, t3);
			m_asm->mov(t3, 0x7fffff);
			m_asm->cmp(_dst, t3);
			m_asm->cmovg(_dst, t3);
		}

		void emitSrc(const LSPInstr& _i, const asmjit::x86::Gpq& _dst)
		{
			switch(_i.src)
			{
			case Src::ASat:
			case Src::BSat:
				emitSat24(_dst, acc(_i.readReg()));
				break;
			case Src::ARaw:
				m_asm->mov(_dst, acc(_i.readReg()));
				m_asm->shl(_dst, 40);
				m_asm->sar(_dst, 40);
				break;
			default:
				m_asm->xor_(_dst.r32(), _dst.r32());
				break;
			}
		}

		void emitAccWrite(const LSPInstr& _i, const asmjit::x86::Gpq& _value)
		{
			if(!_i.replace)
				m_asm->add(_value, acc(_i.liveReg()));
			m_asm->movsxd(acc(_i.destReg), _value.r32());
		}

		// t2 = (eramPos + eramAddr[pc] [+ secondTapOffs]) & 0xffff
		void emitEramAddr(const uint32_t _pc, const bool _secondTap)
		{
			m_asm->movzx(t2.r32(), asmjit::x86::word_ptr(regProg, static_cast<int32_t>(offsetof(LSPProgram, eramAddr) + _pc * 2)));
			m_asm->add(t2.r32(), regEramPos.r32());
			if(_secondTap)
				m_asm->add(t2.r32(), rt(offsetof(LSPRuntime, eramSecondTapOffs)));
			m_asm->and_(t2.r32(), EramMask);
		}

		asmjit::x86::Mem eramAt(const asmjit::x86::Gpq& _index) const
		{
			return asmjit::x86::dword_ptr(regRt, _index, 2, static_cast<int32_t>(offsetof(LSPRuntime, eram)));
		}

		void emitAccStores(const uint8_t _state[6])
		{
			for(int k = 0; k < 6; ++k)
				m_asm->mov(rt(offsetof(LSPRuntime, accs) + k * 4), acc(_state[k]).r32());
		}

		asmjit::Label exitLabel(const uint8_t _state[6])
		{
			uint64_t key = 0;
			for(int k = 0; k < 6; ++k)
				key |= static_cast<uint64_t>(_state[k]) << (k * 8);
			auto it = m_exits.find(key);
			if(it == m_exits.end())
				it = m_exits.emplace(key, m_asm->newLabel()).first;
			return it->second;
		}

		void emitParallelMove(const uint8_t _from[6])
		{
			struct Move { int dst, src; };
			std::vector<Move> pending;
			for(int k = 0; k < 6; ++k)
				if(_from[k] != k)
					pending.push_back({k, _from[k]});

			const auto reg = [](const int _r) { return _r == kMoveTemp ? t3 : acc(_r); };

			while(!pending.empty())
			{
				bool progress = false;
				for(size_t m = 0; m < pending.size(); ++m)
				{
					bool blocked = false;
					for(size_t n = 0; n < pending.size(); ++n)
						if(n != m && pending[n].src == pending[m].dst)
							blocked = true;
					if(blocked)
						continue;
					m_asm->mov(reg(pending[m].dst), reg(pending[m].src));
					pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(m));
					progress = true;
					break;
				}
				if(progress)
					continue;
				const int d = pending[0].dst;
				m_asm->mov(t3, reg(d));
				for(auto& p : pending)
					if(p.src == d)
						p.src = kMoveTemp;
			}
		}

		void emitBudget(const uint8_t _stateIn[6])
		{
			if(!m_needCounter)
				return;
			m_asm->sub(regBudget, 1);
			m_asm->jle(exitLabel(_stateIn));
		}

		void emitSlot(const uint32_t _pc)
		{
			namespace x86 = asmjit::x86;
			auto& a = *m_asm;
			const LSPInstr& i = m_snap[_pc];

			if(i.isJumpTarget)
			{
				if(_pc > 0)
					emitParallelMove(m_snap[_pc - 1].stateOut);
				a.bind(m_labels[_pc]);
			}

			const bool noOp = i.op == Op::Skip || (i.op == Op::Mac && i.zeroCoef && i.src == Src::None);
			const bool pureSkip = noOp && !i.eramRead && !i.eramWrite && i.jump == Jump::None;
			if(pureSkip && !m_needCounter)
				return;

			emitBudget(i.stateIn);

			if(i.eramRead)
			{
				emitEramAddr(_pc, i.eramSecondTap);
				a.mov(t0.r32(), eramAt(t2));
				a.shl(t0.r32(), 4);
				a.mov(rt(offsetof(LSPRuntime, eramReadValue)), t0.r32());
			}

			const bool isAudioOut = i.op == Op::Special && i.slot == SlotAudioOut;

			switch(i.op)
			{
			case Op::Mac:
				if(i.src != Src::None)
				{
					emitSrc(i, t0);
					emitRingStore(t0, i.memOffs);
				}
				if(i.zeroCoef)
					break;
				if(i.immShift)
				{
					a.movsxd(t1, coef(_pc));
					a.shl(t1, i.immShift);
				}
				else
				{
					emitRingLoad(t1, i.memOffs);
					a.movsxd(t3, coef(_pc));
					a.imul(t1, t3);
				}
				a.sar(t1, i.scaler);
				emitAccWrite(i, t1);
				if(i.abs)
				{
					a.mov(t1, acc(i.destReg));
					a.neg(t1);
					a.test(acc(i.destReg), acc(i.destReg));
					a.cmovs(acc(i.destReg), t1);
					a.movsxd(acc(i.destReg), acc(i.destReg).r32());
				}
				break;

			case Op::Mul:
				// The store first: emitSrc's saturation uses t3, which then holds
				// the multiplier coefficient.
				if(i.src != Src::None)
				{
					emitSrc(i, t0);
					emitRingStore(t0, i.memOffs);
				}
				a.movsxd(t3, rt(i.mulCoef2 ? offsetof(LSPRuntime, multiplCoef2) : offsetof(LSPRuntime, multiplCoef1)));
				if(i.mulLower)
				{
					a.and_(t3.r32(), 0xffff);
					a.shr(t3.r32(), 9);
				}
				else
					a.sar(t3, 16);
				if(i.mulZero)
					a.xor_(t1.r32(), t1.r32());
				else
				{
					if(i.immShift)
						a.mov(t1, 1 << i.immShift);
					else
						emitRingLoad(t1, i.memOffs);
					a.imul(t1, t3);
				}
				a.sar(t1, i.scaler + (i.mulLower ? 7 : 0));
				if(i.mulNegate)
					a.neg(t1);
				if(!i.replace)
				{
					emitSat24(t2, acc(i.liveReg()));
					a.add(t1, t2);
				}
				a.movsxd(acc(i.destReg), t1.r32());
				break;

			case Op::Special:
				if(i.imm50d0)
				{
					if(i.zeroCoef)
						break;
					a.movsxd(t3, coef(_pc));
					a.and_(t3.r32(), 0xff);
					if(i.prevMem >= 1 && i.prevMem <= 4)
					{
						a.mov(t1, t3);
						a.shl(t1, (i.prevMem - 1) * 5);
					}
					else
					{
						emitRingLoad(t1, i.prevMem);
						a.imul(t1, t3);
						a.sar(t1, 7);
					}
					a.sar(t1, 1 + i.scaler);
					emitAccWrite(i, t1);
					break;
				}
				emitSrc(i, t0);
				switch(i.slot)
				{
				case SlotJumpIfNegative:
				case SlotJumpIfNonNegative:
					a.xor_(t1.r32(), t1.r32());
					a.test(t0, t0);
					if(i.slot == SlotJumpIfNegative)
						a.setl(x86::dl);
					else
						a.setge(x86::dl);
					a.mov(rt(offsetof(LSPRuntime, jumpPending)), t1.r32());
					break;
				case SlotJumpAlways:
					a.mov(rt(offsetof(LSPRuntime, jumpPending)), 1);
					break;
				case SlotEramWriteLatch:
					a.mov(rt(offsetof(LSPRuntime, eramWriteLatch)), t0.r32());
					break;
				case SlotEramTapAndCoef1:
					if(i.src == Src::ARaw)
					{
						a.mov(t1, acc(i.readReg()));
						a.sar(t1, 10);
						a.mov(rt(offsetof(LSPRuntime, eramSecondTapOffs)), t1.r32());
						a.mov(t1, acc(i.readReg()));
						a.and_(t1.r32(), 0x3ff);
						a.shl(t1.r32(), 13);
						a.mov(rt(offsetof(LSPRuntime, multiplCoef1)), t1.r32());
					}
					break;
				case SlotMulCoef1:
					a.mov(rt(offsetof(LSPRuntime, multiplCoef1)), t0.r32());
					break;
				case SlotMulCoef2:
					a.mov(rt(offsetof(LSPRuntime, multiplCoef2)), t0.r32());
					break;
				case SlotAudioOut:
					a.mov(rt(offsetof(LSPRuntime, audioOut)), t0.r32());
					emitRingStore(t0, 0x78);
					if(_pc < ChannelSplit && i.jump == Jump::None)
						a.mov(rt(offsetof(LSPRuntime, audioOutR)), t0.r32());
					break;
				case SlotEramRead0: case SlotEramRead0 + 1: case SlotEramRead0 + 2: case SlotEramRead0 + 3:
					a.movsxd(t0, rt(offsetof(LSPRuntime, eramReadValue)));
					emitRingStore(t0, static_cast<uint8_t>(0x60 + i.slot));
					break;
				case SlotAudioIn:
					if(_pc >= ChannelSplit)
					{
						a.mov(t1.r32(), rt(offsetof(LSPRuntime, audioInL)));
						a.mov(rt(offsetof(LSPRuntime, audioIn)), t1.r32());
					}
					a.movsxd(t0, rt(offsetof(LSPRuntime, audioIn)));
					emitRingStore(t0, 0x7e);
					break;
				default:
					break;
				}
				if(i.writesAcc)
				{
					a.movsxd(t1, coef(_pc));
					a.imul(t1, t0);
					a.sar(t1, i.scaler);
					emitAccWrite(i, t1);
				}
				break;

			default:
				break;
			}

			if(i.eramWrite)
			{
				emitEramAddr(_pc, false);
				a.mov(t1.r32(), rt(offsetof(LSPRuntime, eramWriteLatch)));
				a.sar(t1.r32(), 4);
				a.mov(eramAt(t2), t1.r32());
			}

			if(i.jump == Jump::None)
				return;

			const asmjit::Label fallthrough = a.newLabel();
			a.cmp(rt(offsetof(LSPRuntime, jumpPending)), 0);
			a.jz(fallthrough);
			a.mov(rt(offsetof(LSPRuntime, jumpPending)), 0);
			if(isAudioOut && i.jumpDest >= 1 && i.jumpDest - 1 < ChannelSplit)
			{
				a.mov(t0.r32(), rt(offsetof(LSPRuntime, audioOut)));
				a.mov(rt(offsetof(LSPRuntime, audioOutR)), t0.r32());
			}
			emitParallelMove(i.stateOut);
			if(i.jumpDest < ProgramWords)
				a.jmp(m_labels[i.jumpDest]);
			else
			{
				static constexpr uint8_t kCanonical[6] = {0, 1, 2, 3, 4, 5};
				a.jmp(exitLabel(kCanonical));
			}
			a.bind(fallthrough);
			if(isAudioOut && _pc < ChannelSplit)
			{
				a.mov(t0.r32(), rt(offsetof(LSPRuntime, audioOut)));
				a.mov(rt(offsetof(LSPRuntime, audioOutR)), t0.r32());
			}
		}

		const LSPProgram& m_program;
		LSPRuntime&       m_rt;

		asmjit::JitRuntime m_jitRuntime;
		Run                m_run = nullptr;

		asmjit::x86::Builder*             m_asm = nullptr;
		const LSPInstr*                   m_snap = nullptr;
		bool                              m_needCounter = false;
		std::vector<asmjit::Label>        m_labels;
		asmjit::Label                     m_epilogue;
		std::map<uint64_t, asmjit::Label> m_exits;
	};
}
