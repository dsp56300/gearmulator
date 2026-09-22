#pragma once

#include <cstddef>
#include <vector>

#include "wasmJit/wasmEmitter.h"
#include "wasmJit/wasmJitHost.h"

#include "../jitHost.h"

#include "lsp_program.h"

namespace lspLib
{
	// WebAssembly backend: the program becomes one WebAssembly function over the memory this
	// code itself runs in, compiled by the JavaScript host (framework/wasmJit). It is the
	// arm64 backend slot for slot - same register plan, same arithmetic in 64-bit values that
	// hold sign-extended 32-bit ones - with two differences that come from the target:
	//
	//   - "Registers" are locals; the host's compiler allocates the real ones.
	//   - Control flow is structured. The slots are cut into segments at the jump targets and
	//     every segment gets a block; a taken jump sets the segment to enter and restarts a loop
	//     around them, where a br_table dispatches. A program without a jump target is one
	//     straight run with no loop at all.
	//
	// Accumulators are flattened onto locals per LSPProgram::assignRegs(); the state is loaded
	// from and stored back to the shared LSPRuntime around the pass, so the interpreter can take
	// over on any sample boundary. Every way out of the pass brings the pipeline into canonical
	// form first, so there is a single epilogue.
	//
	// The host may compile asynchronously: submit() starts a compile and poll() tells when
	// runProgram() may be used. compile() is both, for a host that compiles synchronously.
	class LSPJIT
	{
	public:
		static constexpr chips::JitCompile Compile = chips::JitCompile::Host;
		static constexpr bool Available = Compile != chips::JitCompile::None;

		LSPJIT(const LSPProgram& _program, LSPRuntime& _runtime) : m_program(_program), m_rt(_runtime) {}

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
			m_host.release();
			m_run = nullptr;
		}

		// Emits code for a snapshot of the decoded program and hands it to the host. Only the
		// snapshot and stable addresses (runtime, coefs[], eramAddr[]) are read, and only
		// until this returns.
		void submit(const LSPInstr* _snapshot)
		{
			release();
			const auto module = wasmJit::buildModule(emit(_snapshot));
			m_host.submit(module.data(), module.size(), "v");
			poll();
		}

		using CompileState = wasmJit::HostFunction::State;

		CompileState poll()
		{
			const auto state = m_host.poll();
			if(state == CompileState::Ready)
				m_run = reinterpret_cast<Run>(m_host.pointer());
			return state;
		}

		bool compile(const LSPInstr* _snapshot)
		{
			submit(_snapshot);
			return ready();
		}

		// The module submit() would hand over, for inspection.
		wasmJit::Bytes moduleFor(const LSPInstr* _snapshot) { return wasmJit::buildModule(emit(_snapshot)); }

	private:
		using Run = void(*)();
		using F = wasmJit::Function;
		using Local = wasmJit::Local;

		static constexpr int kMoveTemp = 6;

		wasmJit::Function emit(const LSPInstr* _snapshot)
		{
			using wasmJit::ValType;

			F f;
			m_f = &f;
			m_snap = _snapshot;

			for(auto& acc : m_acc)
				acc = f.local(ValType::I64);
			m_moveTemp = f.local(ValType::I64);
			m_writeLatch = f.local(ValType::I64);
			m_tapOffs = f.local(ValType::I64);
			m_coef1 = f.local(ValType::I64);
			m_coef2 = f.local(ValType::I64);
			m_readValue = f.local(ValType::I64);
			m_t0 = f.local(ValType::I64);
			m_t1 = f.local(ValType::I64);
			m_t3 = f.local(ValType::I64);
			m_bufferPos = f.local(ValType::I32);
			m_eramPos = f.local(ValType::I32);
			m_pred = f.local(ValType::I32);
			m_budget = f.local(ValType::I32);
			m_entry = f.local(ValType::I32);

			m_needCounter = false;
			for(uint32_t pc = 0; pc < ProgramWords; ++pc)
				if(m_snap[pc].jump != Jump::None && m_snap[pc].jumpDest <= pc)
					m_needCounter = true;

			// Segment s starts at m_segmentStart[s]; slot 0 opens the first one.
			m_segmentStart.assign(1, 0);
			m_segmentOf.assign(ProgramWords, 0);
			for(uint32_t pc = 1; pc < ProgramWords; ++pc)
				if(m_snap[pc].isJumpTarget)
					m_segmentStart.push_back(pc);
			for(uint32_t s = 0; s < m_segmentStart.size(); ++s)
				m_segmentOf[m_segmentStart[s]] = s;
			const auto segments = static_cast<uint32_t>(m_segmentStart.size());
			// Slot 0 can be a target too, which makes a loop of a single segment.
			bool needLoop = false;
			for(uint32_t pc = 0; pc < ProgramWords; ++pc)
				needLoop |= m_snap[pc].isJumpTarget;

			// Prologue
			f.address(&m_rt.bufferPos);
			f.i32Load8U();
			f.set(m_bufferPos);
			f.address(&m_rt.eramPos);
			f.i32Load16U();
			f.set(m_eramPos);
			for(int k = 0; k < 6; ++k)
				loadRt(m_acc[k], &m_rt.accs[k]);
			loadRt(m_writeLatch, &m_rt.eramWriteLatch);
			loadRt(m_tapOffs, &m_rt.eramSecondTapOffs);
			loadRt(m_coef1, &m_rt.multiplCoef1);
			loadRt(m_coef2, &m_rt.multiplCoef2);
			loadRt(m_readValue, &m_rt.eramReadValue);
			f.address(&m_rt.jumpPending);
			f.i32Load();
			f.set(m_pred);
			copyRt(&m_rt.audioIn, &m_rt.audioInR);
			if(m_needCounter)
			{
				f.i32Const(static_cast<int32_t>(ProgramWords + 1));
				f.set(m_budget);
			}

			m_exit = f.block();
			if(needLoop)
			{
				m_top = f.loop();
				// Innermost block first: leaving block s lands on the code of segment s.
				std::vector<wasmJit::Label> blocks(segments);
				for(uint32_t s = segments; s-- > 0;)
					blocks[s] = f.block();
				f.get(m_entry);
				f.brTable(blocks, blocks[0]);
				f.end();	// block 0
			}

			for(uint32_t pc = 0; pc < ProgramWords; ++pc)
			{
				if(pc > 0 && m_snap[pc].isJumpTarget)
				{
					// Falling through into a target: canonical form, then the next segment.
					emitParallelMove(m_snap[pc - 1].stateOut);
					f.end();
				}
				emitSlot(pc);
			}

			// Fell off the end
			emitParallelMove(m_snap[ProgramWords - 1].stateOut);
			if(needLoop)
			{
				f.br(m_exit);
				f.end();	// loop
			}
			f.end();	// exit

			// Epilogue: the pipeline is canonical on every path here.
			for(int k = 0; k < 6; ++k)
				storeRt(&m_rt.accs[k], m_acc[k]);
			storeRt(&m_rt.eramWriteLatch, m_writeLatch);
			storeRt(&m_rt.eramSecondTapOffs, m_tapOffs);
			storeRt(&m_rt.multiplCoef1, m_coef1);
			storeRt(&m_rt.multiplCoef2, m_coef2);
			storeRt(&m_rt.eramReadValue, m_readValue);
			f.address(&m_rt.jumpPending);
			f.get(m_pred);
			f.i32Store();

			m_f = nullptr;
			m_snap = nullptr;
			return f;
		}

		// ---- runtime image

		void loadRt(const Local _dst, const int32_t* _field)
		{
			m_f->address(_field);
			m_f->i64Load32S();
			m_f->set(_dst);
		}

		void storeRt(const int32_t* _field, const Local _value)
		{
			m_f->address(_field);
			m_f->get(_value);
			m_f->i64Store32();
		}

		void copyRt(const int32_t* _dst, const int32_t* _src)
		{
			m_f->address(_dst);
			m_f->address(_src);
			m_f->i32Load();
			m_f->i32Store();
		}

		// ---- building blocks; each leaves the stack as it found it unless it says otherwise

		// -> [address of iram[(bufferPos + memOffs) & 0x7f]]
		void pushRingAddr(const uint8_t _memOffs)
		{
			auto& f = *m_f;
			f.get(m_bufferPos);
			f.i32Const(_memOffs);
			f.i32Add();
			f.i32Const(static_cast<int32_t>(DataRingMask));
			f.i32And();
			f.i32Const(2);
			f.i32Shl();
			f.address(&m_rt.iram[0]);
			f.i32Add();
		}

		void emitRingStore(const Local _value, const uint8_t _memOffs)
		{
			pushRingAddr(_memOffs);
			m_f->get(_value);
			m_f->i64Store32();
		}

		// -> [ring value, sign-extended]
		void pushRingLoad(const uint8_t _memOffs)
		{
			pushRingAddr(_memOffs);
			m_f->i64Load32S();
		}

		// -> [coefs[pc], sign-extended]
		void pushCoef(const uint32_t _pc)
		{
			m_f->address(&m_program.coefs[_pc]);
			m_f->i64Load32S();
		}

		// -> [address of eram[(eramPos + eramAddr[pc] (+ tap offset)) & EramMask]]
		void pushEramAddr(const uint32_t _pc, const bool _secondTap)
		{
			auto& f = *m_f;
			f.address(&m_program.eramAddr[_pc]);
			f.i32Load16U();
			f.get(m_eramPos);
			f.i32Add();
			if(_secondTap)
			{
				f.get(m_tapOffs);
				f.i32WrapI64();
				f.i32Add();
			}
			f.i32Const(static_cast<int32_t>(EramMask));
			f.i32And();
			f.i32Const(2);
			f.i32Shl();
			f.address(&m_rt.eram[0]);
			f.i32Add();
		}

		// dst = clamp24(src)
		void emitSat24(const Local _dst, const Local _src)
		{
			auto& f = *m_f;
			f.get(_src);
			f.i64Const(-0x800000);
			f.get(_src);
			f.i64Const(-0x800000);
			f.i64GeS();
			f.select();
			f.set(_dst);
			f.get(_dst);
			f.i64Const(0x7fffff);
			f.get(_dst);
			f.i64Const(0x7fffff);
			f.i64LeS();
			f.select();
			f.set(_dst);
		}

		// The accumulator history value selected by the write control.
		void emitSrc(const LSPInstr& _i, const Local _dst)
		{
			auto& f = *m_f;
			switch(_i.src)
			{
			case Src::ASat:
			case Src::BSat:
				emitSat24(_dst, m_acc[_i.readReg()]);
				break;
			case Src::ARaw:
				f.get(m_acc[_i.readReg()]);
				f.i64Const(40);
				f.i64Shl();
				f.i64Const(40);
				f.i64ShrS();
				f.set(_dst);
				break;
			default:
				f.i64Const(0);
				f.set(_dst);
				break;
			}
		}

		// [value] -> []; dest = int32(replace ? value : live + value)
		void emitAccWrite(const LSPInstr& _i)
		{
			auto& f = *m_f;
			if(!_i.replace)
			{
				f.get(m_acc[_i.liveReg()]);
				f.i64Add();
			}
			f.i64SignExtend32();
			f.set(m_acc[_i.destReg]);
		}

		// Bring the pipeline locals into canonical form: local k must end up holding what
		// local _from[k] holds now.
		void emitParallelMove(const uint8_t _from[6])
		{
			struct Move { int dst, src; };
			std::vector<Move> pending;
			for(int k = 0; k < 6; ++k)
				if(_from[k] != k)
					pending.push_back({k, _from[k]});

			const auto local = [this](const int _r) { return _r == kMoveTemp ? m_moveTemp : m_acc[_r]; };

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
					m_f->get(local(pending[m].src));
					m_f->set(local(pending[m].dst));
					pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(m));
					progress = true;
					break;
				}
				if(progress)
					continue;
				// Every pending move is blocked: park one destination's current value so the
				// moves that read it can take it from the temp.
				const int d = pending[0].dst;
				m_f->get(local(d));
				m_f->set(m_moveTemp);
				for(auto& p : pending)
					if(p.src == d)
						p.src = kMoveTemp;
			}
		}

		void emitBudget(const uint8_t _stateIn[6])
		{
			if(!m_needCounter)
				return;
			auto& f = *m_f;
			f.get(m_budget);
			f.i32Const(1);
			f.i32Sub();
			f.tee(m_budget);
			f.i32Const(0);
			f.i32LeS();
			f.ifThen();
			emitParallelMove(_stateIn);
			f.br(m_exit);
			f.end();
		}

		void emitSlot(const uint32_t _pc)
		{
			auto& f = *m_f;
			const LSPInstr& i = m_snap[_pc];

			const bool noOp = i.op == Op::Skip || (i.op == Op::Mac && i.zeroCoef && i.src == Src::None);
			const bool pureSkip = noOp && !i.eramRead && !i.eramWrite && i.jump == Jump::None;
			if(pureSkip && !m_needCounter)
				return;

			emitBudget(i.stateIn);

			if(i.eramRead)
			{
				pushEramAddr(_pc, i.eramSecondTap);
				f.i32Load();
				f.i32Const(4);
				f.i32Shl();
				f.i64ExtendI32S();
				f.set(m_readValue);
			}

			const bool isAudioOut = i.op == Op::Special && i.slot == SlotAudioOut;

			switch(i.op)
			{
			case Op::Mac:
				if(i.src != Src::None)
				{
					emitSrc(i, m_t0);
					emitRingStore(m_t0, i.memOffs);
				}
				if(i.zeroCoef)
					break;
				if(i.immShift)
				{
					pushCoef(_pc);
					f.i64Const(i.immShift);
					f.i64Shl();
				}
				else
				{
					pushRingLoad(i.memOffs);
					pushCoef(_pc);
					f.i64Mul();
				}
				f.i64Const(i.scaler);
				f.i64ShrS();
				emitAccWrite(i);
				if(i.abs)
				{
					const auto dest = m_acc[i.destReg];
					f.i64Const(0);
					f.get(dest);
					f.i64Sub();
					f.get(dest);
					f.get(dest);
					f.i64Const(0);
					f.i64LtS();
					f.select();
					f.i64SignExtend32();
					f.set(dest);
				}
				break;

			case Op::Mul:
				f.get(i.mulCoef2 ? m_coef2 : m_coef1);
				if(i.mulLower)
				{
					f.i64Const(9);
					f.i64ShrU();
					f.i64Const(0x7f);
					f.i64And();
				}
				else
				{
					f.i64Const(16);
					f.i64ShrS();
				}
				f.set(m_t3);
				if(i.src != Src::None)
				{
					emitSrc(i, m_t0);
					emitRingStore(m_t0, i.memOffs);
				}
				if(i.mulZero)
					f.i64Const(0);
				else
				{
					if(i.immShift)
						f.i64Const(int64_t(1) << i.immShift);
					else
						pushRingLoad(i.memOffs);
					f.get(m_t3);
					f.i64Mul();
				}
				f.i64Const(i.scaler + (i.mulLower ? 7 : 0));
				f.i64ShrS();
				f.set(m_t1);
				if(i.mulNegate)
				{
					f.i64Const(0);
					f.get(m_t1);
					f.i64Sub();
					f.set(m_t1);
				}
				if(!i.replace)
				{
					emitSat24(m_t3, m_acc[i.liveReg()]);
					f.get(m_t1);
					f.get(m_t3);
					f.i64Add();
					f.set(m_t1);
				}
				f.get(m_t1);
				f.i64SignExtend32();
				f.set(m_acc[i.destReg]);
				break;

			case Op::Special:
				if(i.imm50d0)
				{
					if(i.zeroCoef)
						break;
					pushCoef(_pc);
					f.i64Const(0xff);
					f.i64And();
					f.set(m_t3);
					if(i.prevMem >= 1 && i.prevMem <= 4)
					{
						f.get(m_t3);
						f.i64Const((i.prevMem - 1) * 5);
						f.i64Shl();
					}
					else
					{
						pushRingLoad(i.prevMem);
						f.get(m_t3);
						f.i64Mul();
						f.i64Const(7);
						f.i64ShrS();
					}
					f.i64Const(1 + i.scaler);
					f.i64ShrS();
					emitAccWrite(i);
					break;
				}
				emitSrc(i, m_t0);
				switch(i.slot)
				{
				case SlotJumpIfNegative:
					f.get(m_t0);
					f.i64Const(0);
					f.i64LtS();
					f.set(m_pred);
					break;
				case SlotJumpIfNonNegative:
					f.get(m_t0);
					f.i64Const(0);
					f.i64GeS();
					f.set(m_pred);
					break;
				case SlotJumpAlways:
					f.i32Const(1);
					f.set(m_pred);
					break;
				case SlotEramWriteLatch:
					f.get(m_t0);
					f.set(m_writeLatch);
					break;
				case SlotEramTapAndCoef1:
					if(i.src == Src::ARaw)
					{
						const auto read = m_acc[i.readReg()];
						f.get(read);
						f.i64Const(10);
						f.i64ShrS();
						f.set(m_tapOffs);
						f.get(read);
						f.i64Const(0x3ff);
						f.i64And();
						f.i64Const(13);
						f.i64Shl();
						f.set(m_coef1);
					}
					break;
				case SlotMulCoef1:
					f.get(m_t0);
					f.set(m_coef1);
					break;
				case SlotMulCoef2:
					f.get(m_t0);
					f.set(m_coef2);
					break;
				case SlotAudioOut:
					storeRt(&m_rt.audioOut, m_t0);
					emitRingStore(m_t0, 0x78);
					if(_pc < ChannelSplit && i.jump == Jump::None)
						storeRt(&m_rt.audioOutR, m_t0);
					break;
				case SlotEramRead0: case SlotEramRead0 + 1: case SlotEramRead0 + 2: case SlotEramRead0 + 3:
					f.get(m_readValue);
					f.set(m_t0);
					emitRingStore(m_t0, static_cast<uint8_t>(0x60 + i.slot));
					break;
				case SlotAudioIn:
					if(_pc >= ChannelSplit)
						copyRt(&m_rt.audioIn, &m_rt.audioInL);
					loadRt(m_t0, &m_rt.audioIn);
					emitRingStore(m_t0, 0x7e);
					break;
				default:
					break;
				}
				if(i.writesAcc)
				{
					f.get(m_t0);
					pushCoef(_pc);
					f.i64Mul();
					f.i64Const(i.scaler);
					f.i64ShrS();
					emitAccWrite(i);
				}
				break;

			default:
				break;
			}

			if(i.eramWrite)
			{
				pushEramAddr(_pc, false);
				f.get(m_writeLatch);
				f.i64Const(4);
				f.i64ShrS();
				f.i64Store32();
			}

			if(i.jump == Jump::None)
				return;

			// The jump flag set by the previous slot is taken after this one and cleared; a slot
			// entered through a jump sees whatever the flag was. The audio-out sample of a
			// jumping slot follows the modified pc.
			f.get(m_pred);
			f.ifThen();
			{
				f.i32Const(0);
				f.set(m_pred);
				if(isAudioOut && i.jumpDest >= 1 && i.jumpDest - 1 < ChannelSplit)
					copyRt(&m_rt.audioOutR, &m_rt.audioOut);
				emitParallelMove(i.stateOut);
				if(i.jumpDest < ProgramWords)
				{
					f.i32Const(static_cast<int32_t>(m_segmentOf[i.jumpDest]));
					f.set(m_entry);
					f.br(m_top);
				}
				else
				{
					f.br(m_exit);
				}
			}
			f.end();
			if(isAudioOut && _pc < ChannelSplit)
				copyRt(&m_rt.audioOutR, &m_rt.audioOut);
		}

		const LSPProgram& m_program;
		LSPRuntime&       m_rt;

		wasmJit::HostFunction m_host;
		Run                   m_run = nullptr;

		// Per-emit state
		F*               m_f = nullptr;
		const LSPInstr*  m_snap = nullptr;
		bool             m_needCounter = false;
		std::vector<uint32_t> m_segmentStart;
		std::vector<uint32_t> m_segmentOf;
		wasmJit::Label   m_exit;
		wasmJit::Label   m_top;
		Local m_acc[6];
		Local m_moveTemp, m_writeLatch, m_tapOffs, m_coef1, m_coef2, m_readValue, m_t0, m_t1, m_t3;
		Local m_bufferPos, m_eramPos, m_pred, m_budget, m_entry;
	};
}
