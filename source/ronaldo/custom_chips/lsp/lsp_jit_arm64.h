#pragma once

#include <cstddef>
#include <map>
#include <vector>

#include <asmjit/asmjit.h>
#include <asmjit/a64.h>

#include "lsp_program.h"

namespace lspLib
{
	// arm64 backend. One straight-line function per program; the accumulator
	// pipeline is flattened onto x19-x24 by LSPProgram::assignRegs, the delay
	// rings and the live coefficient / ERAM address tables are reached through
	// pointer registers, and the carried state is loaded from and stored to the
	// shared LSPRuntime around the pass, so the interpreter can take over on
	// any sample boundary.
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

		// Emit code for a snapshot of the decoded program. Only the snapshot
		// and stable addresses (runtime, coefs[], eramAddr[]) are read, so a
		// worker can compile while the audio thread keeps patching the tables.
		bool compile(const LSPInstr* _snapshot)
		{
			namespace a64 = asmjit::a64;

			release();
			m_snap = _snapshot;

			asmjit::CodeHolder code;
			code.init(m_jitRuntime.environment());
			a64::Builder a(&code);
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

			// Prologue
			a.stp(a64::x29, a64::x30, a64::Mem(a64::sp, -16).pre());
			a.mov(a64::x29, a64::sp);
			a.stp(a64::x19, a64::x20, a64::Mem(a64::sp, -16).pre());
			a.stp(a64::x21, a64::x22, a64::Mem(a64::sp, -16).pre());
			a.stp(a64::x23, a64::x24, a64::Mem(a64::sp, -16).pre());
			a.stp(a64::x25, a64::x26, a64::Mem(a64::sp, -16).pre());
			a.stp(a64::x27, a64::x28, a64::Mem(a64::sp, -16).pre());

			a.mov(regRt, reinterpret_cast<uint64_t>(&m_rt));
			a.mov(regIram, reinterpret_cast<uint64_t>(&m_rt.iram[0]));
			a.mov(regEram, reinterpret_cast<uint64_t>(&m_rt.eram[0]));
			a.mov(regCoefs, reinterpret_cast<uint64_t>(&m_program.coefs[0]));
			a.mov(regEramAddr, reinterpret_cast<uint64_t>(&m_program.eramAddr[0]));
			a.ldrb(regBufferPos.w(), rt(offsetof(LSPRuntime, bufferPos)));
			a.ldrh(regEramPos.w(), rt(offsetof(LSPRuntime, eramPos)));
			for(int k = 0; k < 6; ++k)
				a.ldrsw(acc(k), rt(offsetof(LSPRuntime, accs) + k * 4));
			a.ldrsw(regWriteLatch, rt(offsetof(LSPRuntime, eramWriteLatch)));
			a.ldrsw(regTapOffs, rt(offsetof(LSPRuntime, eramSecondTapOffs)));
			a.ldrsw(regCoef1, rt(offsetof(LSPRuntime, multiplCoef1)));
			a.ldrsw(regCoef2, rt(offsetof(LSPRuntime, multiplCoef2)));
			a.ldrsw(regReadValue, rt(offsetof(LSPRuntime, eramReadValue)));
			a.ldr(regPred.w(), rt(offsetof(LSPRuntime, jumpPending)));
			a.mov(regSat, 0x7fffff);
			a.ldr(t0.w(), rt(offsetof(LSPRuntime, audioInR)));
			a.str(t0.w(), rt(offsetof(LSPRuntime, audioIn)));
			if(m_needCounter)
				a.mov(regBudget, ProgramWords + 1);

			for(uint32_t pc = 0; pc < ProgramWords; ++pc)
				emitSlot(pc);

			// Fell off the end
			emitAccStores(m_snap[ProgramWords - 1].stateOut);
			a.b(m_epilogue);

			for(const auto& e : m_exits)
			{
				a.bind(e.second);
				uint8_t st[6];
				for(int k = 0; k < 6; ++k)
					st[k] = static_cast<uint8_t>(e.first >> (k * 8));
				emitAccStores(st);
				a.b(m_epilogue);
			}

			a.bind(m_epilogue);
			a.str(regWriteLatch.w(), rt(offsetof(LSPRuntime, eramWriteLatch)));
			a.str(regTapOffs.w(), rt(offsetof(LSPRuntime, eramSecondTapOffs)));
			a.str(regCoef1.w(), rt(offsetof(LSPRuntime, multiplCoef1)));
			a.str(regCoef2.w(), rt(offsetof(LSPRuntime, multiplCoef2)));
			a.str(regReadValue.w(), rt(offsetof(LSPRuntime, eramReadValue)));
			a.str(regPred.w(), rt(offsetof(LSPRuntime, jumpPending)));

			a.ldp(a64::x27, a64::x28, a64::Mem(a64::sp, 16).post(0));
			a.ldp(a64::x25, a64::x26, a64::Mem(a64::sp, 16).post(0));
			a.ldp(a64::x23, a64::x24, a64::Mem(a64::sp, 16).post(0));
			a.ldp(a64::x21, a64::x22, a64::Mem(a64::sp, 16).post(0));
			a.ldp(a64::x19, a64::x20, a64::Mem(a64::sp, 16).post(0));
			a.ldp(a64::x29, a64::x30, a64::Mem(a64::sp, 16).post(0));
			a.ret(a64::x30);

			if(a.finalize() != asmjit::kErrorOk)
				return false;
			return m_jitRuntime.add(&m_run, &code) == asmjit::kErrorOk;
		}

	private:
		using Run = void(*)();

		static constexpr auto regIram      = asmjit::a64::x0;
		static constexpr auto regEram      = asmjit::a64::x1;
		static constexpr auto regCoefs     = asmjit::a64::x2;
		static constexpr auto regEramAddr  = asmjit::a64::x3;
		static constexpr auto regBufferPos = asmjit::a64::x4;
		static constexpr auto regEramPos   = asmjit::a64::x5;
		static constexpr auto regWriteLatch = asmjit::a64::x6;
		static constexpr auto regTapOffs   = asmjit::a64::x7;
		static constexpr auto regCoef1     = asmjit::a64::x8;
		static constexpr auto regCoef2     = asmjit::a64::x9;
		static constexpr auto regReadValue = asmjit::a64::x10;
		static constexpr auto regPred      = asmjit::a64::x11;
		static constexpr auto regBudget    = asmjit::a64::x12;
		static constexpr auto t0 = asmjit::a64::x13;
		static constexpr auto t1 = asmjit::a64::x14;
		static constexpr auto t2 = asmjit::a64::x15;
		static constexpr auto t3 = asmjit::a64::x16;
		static constexpr auto regSat       = asmjit::a64::x25;	// 0x7fffff
		static constexpr auto regRt        = asmjit::a64::x26;
		static constexpr auto regMoveTemp  = asmjit::a64::x27;
		static constexpr int  kAccBase = 19;					// x19-x24
		static constexpr int  kMoveTemp = 6;

		static asmjit::a64::GpX acc(const int _r) { return asmjit::a64::GpX(kAccBase + _r); }
		static asmjit::a64::Mem rt(const size_t _offset) { return asmjit::a64::ptr(regRt, static_cast<int32_t>(_offset)); }

		// t = (bufferPos + memOffs) & 0x7f, zero-extended
		void emitRingAddr(const asmjit::a64::GpX& _t, const uint8_t _memOffs)
		{
			m_asm->add(_t.w(), regBufferPos.w(), _memOffs);
			m_asm->and_(_t.w(), _t.w(), DataRingMask);
		}

		void emitRingStore(const asmjit::a64::GpX& _value, const uint8_t _memOffs)
		{
			emitRingAddr(t2, _memOffs);
			m_asm->str(_value.w(), asmjit::a64::ptr(regIram, t2, asmjit::a64::lsl(2)));
		}

		void emitRingLoad(const asmjit::a64::GpX& _dst, const uint8_t _memOffs)
		{
			emitRingAddr(t2, _memOffs);
			m_asm->ldrsw(_dst, asmjit::a64::ptr(regIram, t2, asmjit::a64::lsl(2)));
		}

		void emitSat24(const asmjit::a64::GpX& _dst, const asmjit::a64::GpX& _src)
		{
			m_asm->cmn(_src, 0x800000);
			m_asm->csinv(_dst, _src, regSat, asmjit::a64::CondCode::kGE);
			m_asm->cmp(_dst, regSat);
			m_asm->csel(_dst, _dst, regSat, asmjit::a64::CondCode::kLE);
		}

		// The accumulator history value selected by the write control.
		void emitSrc(const LSPInstr& _i, const asmjit::a64::GpX& _dst)
		{
			switch(_i.src)
			{
			case Src::ASat:
			case Src::BSat: emitSat24(_dst, acc(_i.readReg())); break;
			case Src::ARaw: m_asm->sbfx(_dst, acc(_i.readReg()), 0, 24); break;
			default:        m_asm->mov(_dst, 0); break;
			}
		}

		// dest = int32(replace ? value : live + value)
		void emitAccWrite(const LSPInstr& _i, const asmjit::a64::GpX& _value)
		{
			if(!_i.replace)
				m_asm->add(_value, _value, acc(_i.liveReg()));
			m_asm->sxtw(acc(_i.destReg), _value.w());
		}

		void emitEramAddr(const uint32_t _pc, const bool _secondTap)
		{
			m_asm->ldrh(t2.w(), asmjit::a64::ptr(regEramAddr, static_cast<int32_t>(_pc * 2)));
			m_asm->add(t2, t2, regEramPos);
			if(_secondTap)
				m_asm->add(t2, t2, regTapOffs);
			m_asm->and_(t2, t2, EramMask);
		}

		void emitAccStores(const uint8_t _state[6])
		{
			for(int k = 0; k < 6; ++k)
				m_asm->str(acc(_state[k]).w(), rt(offsetof(LSPRuntime, accs) + k * 4));
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

		// Bring the pipeline registers into canonical form: register k must
		// end up holding what register _from[k] holds now.
		void emitParallelMove(const uint8_t _from[6])
		{
			struct Move { int dst, src; };
			std::vector<Move> pending;
			for(int k = 0; k < 6; ++k)
				if(_from[k] != k)
					pending.push_back({k, _from[k]});

			const auto reg = [](const int _r) { return _r == kMoveTemp ? regMoveTemp : acc(_r); };

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
				// Every pending move is blocked: park one destination's current
				// value so the moves that read it can take it from the temp.
				const int d = pending[0].dst;
				m_asm->mov(regMoveTemp, reg(d));
				for(auto& p : pending)
					if(p.src == d)
						p.src = kMoveTemp;
			}
		}

		void emitBudget(const uint8_t _stateIn[6])
		{
			if(!m_needCounter)
				return;
			m_asm->subs(regBudget, regBudget, 1);
			m_asm->b(asmjit::a64::CondCode::kLE, exitLabel(_stateIn));
		}

		void emitSlot(const uint32_t _pc)
		{
			namespace a64 = asmjit::a64;
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
				a.ldrsw(regReadValue, a64::ptr(regEram, t2, a64::lsl(2)));
				a.lsl(regReadValue, regReadValue, 4);
				a.sxtw(regReadValue, regReadValue.w());
			}

			const auto coef = a64::ptr(regCoefs, static_cast<int32_t>(_pc * 4));
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
					a.ldrsw(t1, coef);
					a.lsl(t1, t1, i.immShift);
				}
				else
				{
					emitRingLoad(t1, i.memOffs);
					a.ldrsw(t3, coef);
					a.mul(t1, t1, t3);
				}
				a.asr(t1, t1, i.scaler);
				emitAccWrite(i, t1);
				if(i.abs)
				{
					a.cmp(acc(i.destReg), 0);
					a.cneg(acc(i.destReg), acc(i.destReg), a64::CondCode::kLT);
					a.sxtw(acc(i.destReg), acc(i.destReg).w());
				}
				break;

			case Op::Mul:
				if(i.mulLower)
					a.ubfx(t3, i.mulCoef2 ? regCoef2 : regCoef1, 9, 7);
				else
					a.asr(t3, i.mulCoef2 ? regCoef2 : regCoef1, 16);
				if(i.src != Src::None)
				{
					emitSrc(i, t0);
					emitRingStore(t0, i.memOffs);
				}
				if(i.mulZero)
					a.mov(t1, 0);
				else
				{
					if(i.immShift)
						a.mov(t1, 1 << i.immShift);
					else
						emitRingLoad(t1, i.memOffs);
					a.mul(t1, t1, t3);
				}
				a.asr(t1, t1, i.scaler + (i.mulLower ? 7 : 0));
				if(i.mulNegate)
					a.neg(t1, t1);
				if(!i.replace)
				{
					emitSat24(t3, acc(i.liveReg()));
					a.add(t1, t1, t3);
				}
				a.sxtw(acc(i.destReg), t1.w());
				break;

			case Op::Special:
				if(i.imm50d0)
				{
					if(i.zeroCoef)
						break;
					a.ldrsw(t3, coef);
					a.and_(t3, t3, 0xff);
					if(i.prevMem >= 1 && i.prevMem <= 4)
						a.lsl(t1, t3, (i.prevMem - 1) * 5);
					else
					{
						emitRingLoad(t1, i.prevMem);
						a.mul(t1, t1, t3);
						a.asr(t1, t1, 7);
					}
					a.asr(t1, t1, 1 + i.scaler);
					emitAccWrite(i, t1);
					break;
				}
				emitSrc(i, t0);
				switch(i.slot)
				{
				case SlotJumpIfNegative:
					a.cmp(t0, 0);
					a.cset(regPred, a64::CondCode::kLT);
					break;
				case SlotJumpIfNonNegative:
					a.cmp(t0, 0);
					a.cset(regPred, a64::CondCode::kGE);
					break;
				case SlotJumpAlways:
					a.mov(regPred, 1);
					break;
				case SlotEramWriteLatch:
					a.mov(regWriteLatch, t0);
					break;
				case SlotEramTapAndCoef1:
					if(i.src == Src::ARaw)
					{
						a.asr(regTapOffs, acc(i.readReg()), 10);
						a.and_(regCoef1, acc(i.readReg()), 0x3ff);
						a.lsl(regCoef1, regCoef1, 13);
					}
					break;
				case SlotMulCoef1:
					a.mov(regCoef1, t0);
					break;
				case SlotMulCoef2:
					a.mov(regCoef2, t0);
					break;
				case SlotAudioOut:
					a.str(t0.w(), rt(offsetof(LSPRuntime, audioOut)));
					emitRingStore(t0, 0x78);
					if(_pc < ChannelSplit && i.jump == Jump::None)
						a.str(t0.w(), rt(offsetof(LSPRuntime, audioOutR)));
					break;
				case SlotEramRead0: case SlotEramRead0 + 1: case SlotEramRead0 + 2: case SlotEramRead0 + 3:
					a.mov(t0, regReadValue);
					emitRingStore(t0, static_cast<uint8_t>(0x60 + i.slot));
					break;
				case SlotAudioIn:
					if(_pc >= ChannelSplit)
					{
						a.ldr(t1.w(), rt(offsetof(LSPRuntime, audioInL)));
						a.str(t1.w(), rt(offsetof(LSPRuntime, audioIn)));
					}
					a.ldrsw(t0, rt(offsetof(LSPRuntime, audioIn)));
					emitRingStore(t0, 0x7e);
					break;
				default:
					break;
				}
				if(i.writesAcc)
				{
					a.ldrsw(t1, coef);
					a.mul(t1, t0, t1);
					a.asr(t1, t1, i.scaler);
					emitAccWrite(i, t1);
				}
				break;

			default:
				break;
			}

			if(i.eramWrite)
			{
				emitEramAddr(_pc, false);
				a.asr(t1, regWriteLatch, 4);
				a.str(t1.w(), a64::ptr(regEram, t2, a64::lsl(2)));
			}

			if(i.jump == Jump::None)
				return;

			// The jump flag set by the previous slot is taken after this one and
			// cleared; a slot entered through a jump sees whatever the flag was.
			// The audio-out sample of a jumping slot follows the modified pc.
			const asmjit::Label fallthrough = a.newLabel();
			a.cbz(regPred, fallthrough);
			a.mov(regPred, 0);
			if(isAudioOut && i.jumpDest >= 1 && i.jumpDest - 1 < ChannelSplit)
			{
				a.ldr(t0.w(), rt(offsetof(LSPRuntime, audioOut)));
				a.str(t0.w(), rt(offsetof(LSPRuntime, audioOutR)));
			}
			emitParallelMove(i.stateOut);
			if(i.jumpDest < ProgramWords)
				a.b(m_labels[i.jumpDest]);
			else
			{
				static constexpr uint8_t kCanonical[6] = {0, 1, 2, 3, 4, 5};
				a.b(exitLabel(kCanonical));
			}
			a.bind(fallthrough);
			if(isAudioOut && _pc < ChannelSplit)
			{
				a.ldr(t0.w(), rt(offsetof(LSPRuntime, audioOut)));
				a.str(t0.w(), rt(offsetof(LSPRuntime, audioOutR)));
			}
		}

		const LSPProgram& m_program;
		LSPRuntime&       m_rt;

		asmjit::JitRuntime m_jitRuntime;
		Run                m_run = nullptr;

		// Per-compile state
		asmjit::a64::Builder*            m_asm = nullptr;
		const LSPInstr*                  m_snap = nullptr;
		bool                             m_needCounter = false;
		std::vector<asmjit::Label>       m_labels;
		asmjit::Label                    m_epilogue;
		std::map<uint64_t, asmjit::Label> m_exits;
	};
}
