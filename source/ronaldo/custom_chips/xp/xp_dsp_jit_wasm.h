#pragma once

// WebAssembly backend of the XP DSP JIT (framework/wasmJit): the frame function is a WebAssembly function
// over the memory this code itself runs in, compiled by the JavaScript host. Op for op it is the arm64
// backend, with the datapath latches in locals instead of registers - the host's compiler allocates the
// real ones - and stored back in the epilogue, so the interpreter can take over at any frame boundary.
//
// What it covers is what is straight-line or forward-only, because WebAssembly control flow is structured:
//
//   - a static program: the slots in cycle order with the mixer deposits of each cycle ahead of them;
//   - a lockstep pair of static programs: the same, cycle by cycle, with the SDOA-to-SDIA handoff after
//     both slots;
//   - the jumps form: every forward target is a block that ends where the target slot begins, so a taken
//     branch is a br out of it.
//
// A dynamic program (a reachable backward branch, or any branch in a lockstep pair) needs a dispatch on
// the program counter every cycle. It is not compiled: compile() fails and the frame driver stays on the
// interpreter, as it does for a program the native back ends reject.
//
// The host may compile asynchronously: submit() starts a compile and poll() tells when run() may be used.
// compile() is both, for a host that compiles synchronously.

#include "wasmJit/wasmEmitter.h"
#include "wasmJit/wasmJitHost.h"

#include "../jitHost.h"

#include "xp_dsp_program.h"
#include "xp_dsp_state.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace xpLib
{
	class DspJitBackend
	{
	public:
		static constexpr chips::JitCompile Compile = chips::JitCompile::Host;
		static constexpr bool Available = Compile != chips::JitCompile::None;

		using CompileState = wasmJit::HostFunction::State;

		DspJitBackend() = default;
		DspJitBackend(const DspJitBackend&) = delete;
		DspJitBackend& operator=(const DspJitBackend&) = delete;

		DspJitRun run() const { return m_run; }
		size_t codeSize() const { return m_codeSize; }
		const char* lastError() const { return m_lastError; }

		void release()
		{
			m_host.release();
			m_run = nullptr;
		}

		// Emits the frame function for one lowered program, or for a lockstep-linked pair, and hands it to
		// the host. The programs are read only until this returns. False: this program is not compiled.
		bool submit(const FlatProgram& _a, const FlatProgram* _b)
		{
			release();
			wasmJit::Function f({wasmJit::ValType::I32});
			if (!emit(f, _a, _b))
				return false;
			const auto module = wasmJit::buildModule(f);
			m_codeSize = module.size();
			m_host.submit(module.data(), module.size(), "vi");
			poll();
			return true;
		}

		// Failed also when nothing was submitted.
		CompileState poll()
		{
			const auto state = m_host.poll();
			if (state == CompileState::Ready)
				m_run = reinterpret_cast<DspJitRun>(m_host.pointer());
			else if (state == CompileState::Failed)
				m_lastError = "the host failed to compile the module";
			return state == CompileState::Empty ? CompileState::Failed : state;
		}

		bool compile(const FlatProgram& _a, const FlatProgram* _b) { return submit(_a, _b) && m_run != nullptr; }

	private:
		using F = wasmJit::Function;
		using Local = wasmJit::Local;
		using Label = wasmJit::Label;
		using ValType = wasmJit::ValType;

		struct Chip
		{
			const FlatProgram* program = nullptr;
			int index = 0;
			uint16_t slots = 0;
			bool peerListens = false;
			Local state, params, mixBank, procBank, mixer, eramPos, portBusy, skip; // i32
			Local acc, iram, mul, fb, eram, nodeA, nodeB, wv, mask;                  // i64
		};

		static uint32_t off(const size_t _offset) { return static_cast<uint32_t>(_offset); }

		static uint32_t queueOffset(const int _entry, const bool _value)
		{
			return off(offsetof(DspState, pendingEramReads) + _entry * sizeof(DspState::PendingEramRead) +
					   (_value ? offsetof(DspState::PendingEramRead, value) : offsetof(DspState::PendingEramRead, countdown)));
		}

		bool emit(F& _f, const FlatProgram& _a, const FlatProgram* _b)
		{
			m_f = &_f;
			m_lastError = "";
			m_chips.clear();
			m_chips.push_back(Chip{&_a, 0});
			if (_b != nullptr)
				m_chips.push_back(Chip{_b, 1});
			for (const auto& chip : m_chips)
			{
				if (chip.program->dynamic || (chip.program->jumps && m_chips.size() != 1))
				{
					m_lastError = "dynamic programs are not compiled to WebAssembly";
					return false;
				}
			}

			m_frame = _f.param(0);
			for (auto& chip : m_chips)
			{
				chip.slots = chip.program->config.executionSlots;
				for (Local* local : {&chip.state, &chip.params, &chip.mixBank, &chip.procBank, &chip.mixer, &chip.eramPos,
									 &chip.portBusy, &chip.skip})
					*local = _f.local(ValType::I32);
				for (Local* local : {&chip.acc, &chip.iram, &chip.mul, &chip.fb, &chip.eram, &chip.nodeA, &chip.nodeB,
									 &chip.wv, &chip.mask})
					*local = _f.local(ValType::I64);
			}
			for (auto& t : m_t)
				t = _f.local(ValType::I64);
			for (auto& c : m_cap)
				c = _f.local(ValType::I64);
			for (auto& i : m_i)
				i = _f.local(ValType::I32);
			if (m_chips.size() == 2)
			{
				m_chips[0].peerListens = m_chips[1].program->config.serialInputEnabled != 0;
				m_chips[1].peerListens = m_chips[0].program->config.serialInputEnabled != 0;
			}

			for (auto& chip : m_chips)
				emitChipEntry(chip);
			if (m_chips.size() == 1 && m_chips[0].program->jumps)
				emitJumpsProgram(m_chips[0]);
			else
				emitCycles();
			for (auto& chip : m_chips)
				emitChipExit(chip);

			m_f = nullptr;
			return m_lastError[0] == 0;
		}

		// ---- arithmetic helpers; [value] -> [value] unless they say otherwise ----

		// [x] -> [clamp(x, -2^(bits-1), 2^(bits-1) - 1)]; clobbers t3.
		void saturate(const unsigned _bits)
		{
			auto& f = *m_f;
			const int64_t minimum = -(int64_t{1} << (_bits - 1));
			const int64_t maximum = (int64_t{1} << (_bits - 1)) - 1;
			f.tee(m_t[3]);
			f.i64Const(minimum);
			f.get(m_t[3]);
			f.i64Const(minimum);
			f.i64GeS();
			f.select();
			f.tee(m_t[3]);
			f.i64Const(maximum);
			f.get(m_t[3]);
			f.i64Const(maximum);
			f.i64LeS();
			f.select();
		}

		void signExtend(const unsigned _bits)
		{
			m_f->i64Const(64 - _bits);
			m_f->i64Shl();
			m_f->i64Const(64 - _bits);
			m_f->i64ShrS();
		}

		// The 24-bit memory image of a value: saturated, masked.
		void encode24()
		{
			saturate(24);
			m_f->i64Const(0xffffff);
			m_f->i64And();
		}

		// [x] -> [x / 2^shift], truncating toward zero; clobbers t3.
		void divideTruncating(const unsigned _shift)
		{
			auto& f = *m_f;
			f.tee(m_t[3]);
			f.get(m_t[3]);
			f.i64Const(63);
			f.i64ShrS();
			f.i64Const(64 - _shift);
			f.i64ShrU();
			f.i64Add();
			f.i64Const(_shift);
			f.i64ShrS();
		}

		// Stores whether the multiply numerator in _numerator is negative with a nonzero remainder below
		// 2^_shift.
		void emitNegativeFraction(const Chip& _chip, const Local _numerator, const unsigned _shift)
		{
			auto& f = *m_f;
			f.get(_chip.state);
			f.get(_numerator);
			f.i64Const((int64_t{1} << _shift) - 1);
			f.i64And();
			f.i64Const(0);
			f.i64Ne();
			f.get(_numerator);
			f.i64Const(63);
			f.i64ShrU();
			f.i32WrapI64();
			f.i32And();
			f.i32Store8(off(offsetof(DspState, multiplyNegativeFraction)));
		}

		// ---- memory ----

		// -> [base address]; the cell is at `offset` from it.
		void pushIramBase(const Chip& _chip, const uint8_t _bank, const uint8_t _index, uint32_t& _offset)
		{
			const auto byteOffset = static_cast<uint32_t>(_index) * 4;
			switch (static_cast<DspIramBank>(_bank))
			{
			case DspIramBank::mixer:
				m_f->get(_chip.mixBank);
				_offset = byteOffset;
				break;
			case DspIramBank::processing:
				m_f->get(_chip.procBank);
				_offset = byteOffset;
				break;
			case DspIramBank::direct1:
				m_f->get(_chip.state);
				_offset = off(offsetof(DspState, iram1)) + byteOffset;
				break;
			case DspIramBank::direct2:
				m_f->get(_chip.state);
				_offset = off(offsetof(DspState, iram2)) + byteOffset;
				break;
			default:
				m_f->get(_chip.state);
				_offset = off(offsetof(DspState, iram3)) + byteOffset;
				break;
			}
		}

		// [offset(i32)] -> [address of eram[(eramPos + offset) & 0xffff]]
		void eramAddress(const Chip& _chip)
		{
			auto& f = *m_f;
			f.get(_chip.eramPos);
			f.i32Add();
			f.i32Const(0xffff);
			f.i32And();
			f.i32Const(2);
			f.i32Shl();
			f.get(_chip.state);
			f.i32Add();
			f.i32Const(static_cast<int32_t>(offsetof(DspState, eram)));
			f.i32Add();
		}

		// [address] -> [the 24-bit word there, sign-extended]
		void load24()
		{
			m_f->i64Load32U();
			signExtend(24);
		}

		// -> [params.param[slot], sign-extended]
		void pushParam(const Chip& _chip, const uint16_t _slot)
		{
			m_f->get(_chip.params);
			m_f->i64Load32S(off(offsetof(DspParams, param) + _slot * 4));
		}

		// -> [params.eramOffset[slot] (i32)]
		void pushEramOffset(const Chip& _chip, const uint16_t _slot)
		{
			m_f->get(_chip.params);
			m_f->i32Load16U(off(offsetof(DspParams, eramOffset) + _slot * 2));
		}

		void storeState8(const Chip& _chip, const size_t _offset, const int32_t _value)
		{
			m_f->get(_chip.state);
			m_f->i32Const(_value);
			m_f->i32Store8(off(_offset));
		}

		void storeState32(const Chip& _chip, const size_t _offset, const int32_t _value)
		{
			m_f->get(_chip.state);
			m_f->i32Const(_value);
			m_f->i32Store(off(_offset));
		}

		// ---- prologue / epilogue ----
		void emitChipEntry(Chip& _chip)
		{
			auto& f = *m_f;
			const auto i = static_cast<size_t>(_chip.index);
			const auto pointer = [&](const Local _dst, const size_t _offset)
			{
				f.get(m_frame);
				f.i32Load(off(_offset + i * sizeof(void*)));
				f.set(_dst);
			};
			pointer(_chip.state, offsetof(DspJitFrame, state));
			pointer(_chip.params, offsetof(DspJitFrame, params));
			pointer(_chip.mixBank, offsetof(DspJitFrame, mixerBank));
			pointer(_chip.procBank, offsetof(DspJitFrame, processingBank));
			pointer(_chip.mixer, offsetof(DspJitFrame, mixer));

			const auto latch = [&](const Local _dst, const size_t _offset)
			{
				f.get(_chip.state);
				f.i64Load(off(_offset));
				f.set(_dst);
			};
			latch(_chip.acc, offsetof(DspState, accumulator));
			latch(_chip.iram, offsetof(DspState, iramReadLatch));
			latch(_chip.mul, offsetof(DspState, multiplyResultLatch));
			latch(_chip.fb, offsetof(DspState, multiplyFeedbackLatch));
			latch(_chip.eram, offsetof(DspState, eramReadLatch));
			latch(_chip.wv, offsetof(DspState, eramPendingWriteValue));
			latch(_chip.mask, offsetof(DspState, mixerInitialized));
			latch(_chip.nodeA, offsetof(DspState, serialInputNode));
			latch(_chip.nodeB, offsetof(DspState, serialInputNode) + sizeof(int64_t));
			f.get(_chip.state);
			f.i32Load(off(offsetof(DspState, eramPos)));
			f.set(_chip.eramPos);
		}

		void emitChipExit(Chip& _chip)
		{
			auto& f = *m_f;
			const auto latch = [&](const size_t _offset, const Local _src)
			{
				f.get(_chip.state);
				f.get(_src);
				f.i64Store(off(_offset));
			};
			latch(offsetof(DspState, accumulator), _chip.acc);
			latch(offsetof(DspState, iramReadLatch), _chip.iram);
			latch(offsetof(DspState, multiplyResultLatch), _chip.mul);
			latch(offsetof(DspState, multiplyFeedbackLatch), _chip.fb);
			latch(offsetof(DspState, eramReadLatch), _chip.eram);
			latch(offsetof(DspState, eramPendingWriteValue), _chip.wv);
			latch(offsetof(DspState, mixerInitialized), _chip.mask);
			latch(offsetof(DspState, serialInputNode), _chip.nodeA);
			latch(offsetof(DspState, serialInputNode) + sizeof(int64_t), _chip.nodeB);

			// The frame-end carry of a static program.
			const auto& carry = _chip.program->carry;
			storeState32(_chip, offsetof(DspState, outputWordPosition), static_cast<int32_t>(carry.outputWordPosition));
			storeState8(_chip, offsetof(DspState, dacPortPosition), carry.dacPortPosition);
			for (size_t bus = 0; bus < dsp::nSerialBuses; ++bus)
				storeState32(_chip, offsetof(DspState, serialOutputCount) + bus * 4, carry.serialOutputCount[bus]);
			if (!carry.staticRegion)
				return;
			storeState8(_chip, offsetof(DspState, eramPrefixPending), carry.prefixPendingAtEnd);
			if (carry.hasArm)
			{
				storeState8(_chip, offsetof(DspState, eramPendingWrite), carry.lastArmIsWrite);
				f.get(_chip.state);
				f.get(_chip.params);
				f.i32Load8U(off(offsetof(DspParams, eramOffsetHigh) + carry.lastArmSlot));
				f.i32Store8(off(offsetof(DspState, eramOffsetHigh)));
			}
			for (int e = 0; e < 2; ++e)
			{
				const auto& entry = carry.queue[e];
				storeState32(_chip, queueOffset(e, false), entry.countdown);
				if (entry.countdown < 0 || entry.indexed)
					continue;
				f.get(_chip.state);
				pushEramOffset(_chip, entry.slot);
				eramAddress(_chip);
				load24();
				f.i64Store32(queueOffset(e, true));
			}
		}

		// The cycle-major skeleton: the slots inline, the pair handoff after both.
		void emitCycles()
		{
			size_t cycles = 0;
			for (const auto& chip : m_chips)
				cycles = std::max<size_t>(cycles, chip.slots);
			for (size_t cycle = 0; cycle < cycles; ++cycle)
			{
				for (auto& chip : m_chips)
					if (cycle < chip.slots)
					{
						emitDeposits(chip, cycle);
						emitSlot(chip, cycle);
					}
				if (m_chips.size() == 2)
					emitExchange(cycle);
			}
		}

		// The jumps form: straight-line slots with forward jumps. Every target is a block that ends where
		// its slot begins, the latest target outermost, so the ends come in slot order. The skip local
		// accumulates the slots jumped over; the frame ends after the slot whose number minus the skips is
		// the last cycle.
		void emitJumpsProgram(Chip& _chip)
		{
			auto& f = *m_f;
			const auto& flat = *_chip.program;
			const size_t budget = flat.config.executionSlots;
			std::vector<uint8_t> isTarget(flat.slotsLowered, 0);
			for (const auto& op : flat.ops)
				if (op.kind == FlatOpKind::branch && op.index < flat.slotsLowered)
					isTarget[op.index] = 1;

			const Label exit = f.block();
			std::vector<Label> targets(flat.slotsLowered);
			for (size_t pc = flat.slotsLowered; pc-- > 0;)
				if (isTarget[pc])
					targets[pc] = f.block();

			m_jumpsMode = true;
			for (size_t pc = 0; pc < flat.slotsLowered; ++pc)
			{
				if (isTarget[pc])
					f.end();
				m_pendingBranch = false;
				emitSlot(_chip, pc);
				if (pc + 1 >= budget)
				{
					f.get(_chip.skip);
					f.i32Const(static_cast<int32_t>(pc + 1 - budget));
					f.i32Eq();
					f.brIf(exit);
				}
				if (!m_pendingBranch || m_pendingTarget >= flat.slotsLowered || m_pendingTarget <= pc + 1)
					continue;
				const auto skipped = static_cast<int32_t>(m_pendingTarget - pc - 1);
				const auto take = [&]
				{
					f.get(_chip.skip);
					f.i32Const(skipped);
					f.i32Add();
					f.set(_chip.skip);
					f.br(targets[m_pendingTarget]);
				};
				if (m_pendingCondition == DspBranch::always)
				{
					take();
					continue;
				}
				f.get(_chip.acc);
				f.i64Const(0);
				switch (m_pendingCondition)
				{
				case DspBranch::eq0: f.i64Eq(); break;
				case DspBranch::ne0: f.i64Ne(); break;
				case DspBranch::ge0: f.i64GeS(); break;
				case DspBranch::lt0: f.i64LtS(); break;
				case DspBranch::gt0: f.i64GtS(); break;
				default: f.i64LeS(); break;
				}
				f.ifThen();
				take();
				f.end();
			}
			f.end(); // exit
			m_jumpsMode = false;
		}

		// ---- mixer deposits ----
		void emitDeposits(Chip& _chip, const size_t _cycle)
		{
			auto& f = *m_f;
			const auto phase = _cycle & 3;
			if (phase == 1)
				return;
			const auto voice = _cycle >> 2;
			const Local dest = m_i[0];
			// A null mixer frame means the deposits were hoisted ahead of the frame, or there are none.
			f.get(_chip.mixer);
			f.ifThen();
			const auto deposit = [&](const size_t _send)
			{
				const auto offset = (voice * 4 + _send) * sizeof(DspMixerSend);
				f.get(_chip.mixer);
				f.i32Load(off(offset + offsetof(DspMixerSend, destination)));
				f.tee(dest);
				f.i32Const(static_cast<int32_t>(dsp::nIramSlots));
				f.i32LtU();
				f.ifThen();
				{
					// The cell's address, for the store at the end.
					f.get(_chip.mixBank);
					f.get(dest);
					f.i32Const(2);
					f.i32Shl();
					f.i32Add();
					f.tee(m_i[1]);

					f.get(_chip.mask);
					f.get(dest);
					f.i64ExtendI32U();
					f.i64ShrU();
					f.i32WrapI64();
					f.i32Const(1);
					f.i32And();
					f.ifThen();
					{
						f.get(m_i[1]);
						load24();
						f.set(m_t[0]);
					}
					f.orElse();
					{
						// First send of the frame: the cell starts from zero.
						f.get(_chip.mask);
						f.i64Const(1);
						f.get(dest);
						f.i64ExtendI32U();
						f.i64Shl();
						f.i64Or();
						f.set(_chip.mask);
						f.i64Const(0);
						f.set(m_t[0]);
					}
					f.end();
					f.get(m_t[0]);
					f.get(_chip.mixer);
					f.i64Load(off(offset + offsetof(DspMixerSend, contribution)));
					f.i64Add();
					encode24();
					f.i64Store32();
				}
				f.end();
			};
			if (phase == 0)
				deposit(0);
			else if (phase == 2)
			{
				deposit(1);
				deposit(2);
			}
			else
				deposit(3);
			f.end();
		}

		// ---- the lockstep word handoff (static programs: the emitting slots are known) ----
		void emitExchange(const size_t _cycle)
		{
			auto& f = *m_f;
			for (size_t from = 0; from < 2; ++from)
			{
				auto& source = m_chips[from];
				auto& peer = m_chips[1 - from];
				if (!source.peerListens || _cycle >= source.slots)
					continue;
				const auto& flat = *source.program;
				for (size_t i = flat.slotToOp[_cycle]; i < flat.slotToOp[_cycle + 1]; ++i)
				{
					const auto& op = flat.ops[i];
					if (op.kind != FlatOpKind::emitA)
						continue;
					f.get(source.state);
					f.i64Load32S(off(offsetof(DspState, serialOutput) + op.a * 4));
					f.set(peer.nodeA);
				}
			}
		}

		// ---- one slot ----
		void emitSlot(Chip& _chip, const size_t _pc)
		{
			const auto& flat = *_chip.program;
			for (size_t i = flat.slotToOp[_pc]; i < flat.slotToOp[_pc + 1]; ++i)
				emitOp(_chip, flat.ops[i]);
		}

		// -> [the multiplier input]
		void pushMultiplySource(Chip& _chip, const DspMultiplyInput _input)
		{
			switch (_input)
			{
			case DspMultiplyInput::feedbackLatch:
				m_f->get(_chip.fb);
				break;
			case DspMultiplyInput::accumulatorSat24:
				m_f->get(_chip.acc);
				saturate(24);
				break;
			case DspMultiplyInput::iramReadLatch:
				m_f->get(_chip.iram);
				break;
			case DspMultiplyInput::eramReadLatch:
				m_f->get(_chip.eram);
				break;
			case DspMultiplyInput::serialNodeA:
				m_f->get(_chip.nodeA);
				break;
			default:
				m_f->get(_chip.nodeB);
				break;
			}
		}

		// -> [multiplyNegativeFraction (0/1) as i64]
		void pushNegativeFraction(const Chip& _chip)
		{
			m_f->get(_chip.state);
			m_f->i64Load8U(off(offsetof(DspState, multiplyNegativeFraction)));
		}

		// [a, b] -> [min or max of them]; clobbers t1, t2.
		void minMax(const bool _max)
		{
			auto& f = *m_f;
			f.set(m_t[2]);
			f.set(m_t[1]);
			f.get(m_t[1]);
			f.get(m_t[2]);
			f.get(m_t[1]);
			f.get(m_t[2]);
			if (_max)
				f.i64GtS();
			else
				f.i64LtS();
			f.select();
		}

		// acc = f(acc, iram, mul, param), the parameter in t0. A primary op wraps the result to 29 bits here;
		// a parallel op keeps the raw result for its transform, which wraps afterwards (an absolute value of
		// a result beyond 28 bits differs between the two orders).
		void emitAlu(Chip& _chip, const DspAlu _alu, const bool _wrap = true)
		{
			auto& f = *m_f;
			const Local acc = _chip.acc, iram = _chip.iram, mul = _chip.mul, param = m_t[0];
			const auto sum = [&](const Local _x, const Local _y)
			{
				f.get(_x);
				f.get(_y);
				f.i64Add();
			};
			const auto difference = [&](const Local _x, const Local _y)
			{
				f.get(_x);
				f.get(_y);
				f.i64Sub();
			};
			const auto maskedAcc = [&]
			{
				f.get(acc);
				f.i64Const(0x1fffffff);
				f.i64And();
				f.get(param);
			};
			switch (_alu)
			{
			case DspAlu::hold:
				return;
			case DspAlu::accPlusIram: sum(acc, iram); break;
			case DspAlu::accPlusMul: sum(acc, mul); break;
			case DspAlu::iram: f.get(iram); break;
			case DspAlu::mul: f.get(mul); break;
			case DspAlu::negAcc:
				f.i64Const(0);
				f.get(acc);
				f.i64Sub();
				break;
			case DspAlu::iramMinusAcc: difference(iram, acc); break;
			case DspAlu::mulMinusAcc: difference(mul, acc); break;
			case DspAlu::iramPlusMul: sum(iram, mul); break;
			case DspAlu::minAccIram:
				f.get(acc);
				f.get(iram);
				minMax(false);
				break;
			case DspAlu::maxAccIram:
				f.get(acc);
				f.get(iram);
				minMax(true);
				break;
			case DspAlu::accPlusMulShr13:
				f.get(acc);
				f.get(mul);
				f.i64Const(13);
				f.i64ShrS();
				f.i64Add();
				break;
			case DspAlu::mulShr13:
				f.get(mul);
				f.i64Const(13);
				f.i64ShrS();
				break;
			case DspAlu::andImm:
				maskedAcc();
				f.i64And();
				break;
			case DspAlu::orImm:
				maskedAcc();
				f.i64Or();
				break;
			case DspAlu::xorImm:
				maskedAcc();
				f.i64Xor();
				break;
			case DspAlu::minImm:
				f.get(acc);
				f.get(param);
				minMax(false);
				break;
			case DspAlu::maxImm:
				f.get(acc);
				f.get(param);
				minMax(true);
				break;
			case DspAlu::sameSignMinElseMax:
			case DspAlu::sameSignMaxElseMin:
				{
					// Signs differ when the xor of the operands is negative.
					const bool minWhenSame = _alu == DspAlu::sameSignMinElseMax;
					f.get(acc);
					f.get(param);
					minMax(!minWhenSame);	// picked when the signs are the same
					f.set(m_t[3]);
					f.get(acc);
					f.get(param);
					minMax(minWhenSame);	// picked when they differ
					f.get(m_t[3]);
					f.get(acc);
					f.get(param);
					f.i64Xor();
					f.i64Const(0);
					f.i64LtS();
					f.select();
					break;
				}
			case DspAlu::accPlusImm: sum(acc, param); break;
			case DspAlu::iramPlusImm: sum(iram, param); break;
			case DspAlu::mulPlusImm: sum(mul, param); break;
			case DspAlu::negAccPlusImm: difference(param, acc); break;
			case DspAlu::accPlusMulPlusIram:
				sum(acc, mul);
				f.get(iram);
				f.i64Add();
				break;
			case DspAlu::iramPlusMulMinusAcc:
				sum(iram, mul);
				f.get(acc);
				f.i64Sub();
				break;
			// The parallel D-F functions also consume the discarded negative fraction of the preceding multiply.
			case DspAlu::accPlusMulMinusIram:
				sum(acc, mul);
				f.get(iram);
				f.i64Sub();
				pushNegativeFraction(_chip);
				f.i64Sub();
				break;
			case DspAlu::mulMinusIramMinusAcc:
				difference(mul, iram);
				f.get(acc);
				f.i64Sub();
				pushNegativeFraction(_chip);
				f.i64Const(1);
				f.i64Shl();
				f.i64Sub();
				break;
			case DspAlu::mulMinusIram:
				difference(mul, iram);
				pushNegativeFraction(_chip);
				f.i64Const(1);
				f.i64Shl();
				f.i64Sub();
				break;
			}
			if (_wrap)
				signExtend(29);
			f.set(acc);
		}

		void emitTransform(Chip& _chip, const DspTransform _transform, const bool _sext24)
		{
			auto& f = *m_f;
			const Local acc = _chip.acc;
			bool narrowed = false;
			switch (_transform)
			{
			case DspTransform::absolute:
				f.i64Const(0);
				f.get(acc);
				f.i64Sub();
				f.get(acc);
				f.get(acc);
				f.i64Const(0);
				f.i64LtS();
				f.select();
				f.set(acc);
				break;
			case DspTransform::sext24:
				f.get(acc);
				signExtend(24);
				f.set(acc);
				narrowed = true;
				break;
			case DspTransform::lfsr:
				f.get(acc);
				f.i64Const(23);
				f.i64ShrS();
				f.get(acc);
				f.i64Const(6);
				f.i64ShrS();
				f.i64Xor();
				f.get(acc);
				f.i64Const(1);
				f.i64ShrS();
				f.i64Xor();
				f.i64Const(1);
				f.i64And();
				f.get(acc);
				f.i64Const(1);
				f.i64Shl();
				f.i64Or();
				f.set(acc);
				break;
			case DspTransform::foldMirror:
				f.get(acc);
				f.i64Const(0xffffff);
				f.i64And();
				f.set(acc);
				f.get(acc);
				f.i64Const(0x7fffff);
				f.i64Xor();
				f.get(acc);
				f.get(acc);
				f.get(acc);
				f.i64Const(1);
				f.i64ShrU();
				f.i64Xor();
				f.i64Const(0x400000);
				f.i64And();
				f.i64Const(0);
				f.i64Ne();
				f.select();
				signExtend(24);
				f.set(acc);
				narrowed = true;
				break;
			case DspTransform::onesComplementNegative:
				f.get(acc);
				signExtend(24);
				f.set(acc);
				f.get(acc);
				f.i64Const(-1);
				f.i64Xor();
				f.get(acc);
				f.get(acc);
				f.i64Const(0);
				f.i64LtS();
				f.select();
				f.set(acc);
				narrowed = true;
				break;
			default:
				break;
			}
			if (_sext24)
			{
				f.get(acc);
				signExtend(24);
				f.set(acc);
				narrowed = true;
			}
			if (!narrowed)
			{
				f.get(acc);
				signExtend(29);
				f.set(acc);
			}
		}

		void emitConsume(Chip& _chip, const size_t _port)
		{
			auto& f = *m_f;
			const Local index = m_i[0];
			f.get(_chip.state);
			f.i32Load(off(offsetof(DspState, serialInputIndex) + _port * 4));
			f.tee(index);
			f.get(_chip.state);
			f.i32Load(off(offsetof(DspState, serialInputCount) + _port * 4));
			f.i32LtU();
			f.ifThen();
			{
				f.get(_chip.state);
				f.get(index);
				f.i32Const(2);
				f.i32Shl();
				f.i32Add();
				f.i64Load32S(off(offsetof(DspState, serialInput) + _port * dsp::nSerialWords * 4));
				f.set(_port == 0 ? _chip.nodeA : _chip.nodeB);
				f.get(_chip.state);
				f.get(index);
				f.i32Const(1);
				f.i32Add();
				f.i32Store(off(offsetof(DspState, serialInputIndex) + _port * 4));
			}
			f.end();
		}

		// Queue a read into the first free runtime entry: the value in _value, countdown _countdown.
		void emitQueueRead(Chip& _chip, const Local _value, const int _countdown)
		{
			auto& f = *m_f;
			const Label done = f.block();
			for (int e = 0; e < 2; ++e)
			{
				f.get(_chip.state);
				f.i32Load(queueOffset(e, false));
				f.i32Const(0);
				f.i32LtS();
				f.ifThen();
				{
					f.get(_chip.state);
					f.get(_value);
					f.i64Store32(queueOffset(e, true));
					storeState32(_chip, queueOffset(e, false), _countdown);
					f.br(done);
				}
				f.end();
			}
			f.end();
		}

		// -> [(acc >> 12) & 0xffff (i32)], also latched into eramIndexedOffset
		void pushIndexedOffset(Chip& _chip)
		{
			auto& f = *m_f;
			f.get(_chip.acc);
			f.i64Const(12);
			f.i64ShrU();
			f.i64Const(0xffff);
			f.i64And();
			f.i32WrapI64();
			f.set(m_i[0]);
			f.get(_chip.state);
			f.get(m_i[0]);
			f.i32Store16(off(offsetof(DspState, eramIndexedOffset)));
			f.get(m_i[0]);
		}

		void emitOp(Chip& _chip, const FlatOp& _op)
		{
			auto& f = *m_f;
			switch (_op.kind)
			{
			case FlatOpKind::eramArm:
				f.get(_op.a ? _chip.iram : _chip.acc);
				f.set(_chip.wv);
				break;
			case FlatOpKind::eramWrite:
				pushEramOffset(_chip, _op.index);
				eramAddress(_chip);
				f.get(_chip.wv);
				encode24();
				f.i64Store32();
				break;
			case FlatOpKind::eramLand:
				pushEramOffset(_chip, _op.index);
				eramAddress(_chip);
				load24();
				f.set(_chip.eram);
				f.get(_chip.state);
				f.get(_chip.eram);
				f.i64Store32(queueOffset(_op.a, true));
				break;
			case FlatOpKind::eramIndexed:
				f.get(_chip.state);
				pushIndexedOffset(_chip);
				eramAddress(_chip);
				load24();
				f.i64Store32(queueOffset(_op.a, true));
				break;
			case FlatOpKind::eramLandIndexed:
				f.get(_chip.state);
				f.i64Load32S(queueOffset(_op.a, true));
				f.set(_chip.eram);
				break;
			case FlatOpKind::eramGeneric:
				f.get(_chip.state);
				f.i32Load8U(off(offsetof(DspState, eramPrefixPending)));
				f.ifThen();
				{
					// Second word: consume the pending command.
					f.get(_chip.params);
					f.i32Load16U(off(offsetof(DspParams, eramOffsetLow) + _op.index * 2));
					f.get(_chip.state);
					f.i32Load8U(off(offsetof(DspState, eramOffsetHigh)));
					f.i32Const(9);
					f.i32Shl();
					f.i32Or();
					eramAddress(_chip);
					f.set(m_i[1]);
					f.get(_chip.state);
					f.i32Load8U(off(offsetof(DspState, eramPendingWrite)));
					f.ifThen();
					{
						f.get(m_i[1]);
						f.get(_chip.wv);
						encode24();
						f.i64Store32();
					}
					f.orElse();
					{
						f.get(m_i[1]);
						load24();
						f.set(m_t[2]);
						emitQueueRead(_chip, m_t[2], 1);
					}
					f.end();
					storeState8(_chip, offsetof(DspState, eramPrefixPending), 0);
					f.i32Const(1);
					f.set(_chip.portBusy);
				}
				f.orElse();
				{
					f.i32Const(0);
					f.set(_chip.portBusy);
					if (_op.a != static_cast<uint8_t>(DspEramArm::none))
					{
						storeState8(_chip, offsetof(DspState, eramPrefixPending), 1);
						storeState8(_chip, offsetof(DspState, eramPendingWrite),
									_op.a != static_cast<uint8_t>(DspEramArm::read) ? 1 : 0);
						f.get(_chip.state);
						f.get(_chip.params);
						f.i32Load8U(off(offsetof(DspParams, eramOffsetHigh) + _op.index));
						f.i32Store8(off(offsetof(DspState, eramOffsetHigh)));
						f.get(_op.a == static_cast<uint8_t>(DspEramArm::writeIramLatch) ? _chip.iram : _chip.acc);
						f.set(_chip.wv);
					}
				}
				f.end();
				break;
			case FlatOpKind::eramAdvance:
				for (int e = 0; e < 2; ++e)
				{
					f.get(_chip.state);
					f.i32Load(queueOffset(e, false));
					f.tee(m_i[0]);
					f.i32Const(0);
					f.i32GeS();
					f.ifThen();
					{
						f.get(_chip.state);
						f.get(m_i[0]);
						f.i32Const(1);
						f.i32Sub();
						f.tee(m_i[0]);
						f.i32Store(queueOffset(e, false));
						f.get(m_i[0]);
						f.i32Eqz();
						f.ifThen();
						{
							f.get(_chip.state);
							f.i64Load32S(queueOffset(e, true));
							f.set(_chip.eram);
							storeState32(_chip, queueOffset(e, false), -1);
						}
						f.end();
					}
					f.end();
				}
				break;
			case FlatOpKind::eramIndexedGeneric:
				f.get(_chip.portBusy);
				f.i32Eqz();
				f.ifThen();
				{
					pushIndexedOffset(_chip);
					eramAddress(_chip);
					load24();
					f.set(m_t[2]);
					emitQueueRead(_chip, m_t[2], 2);
				}
				f.end();
				break;
			case FlatOpKind::iramWriteAcc:
				{
					uint32_t offset = 0;
					pushIramBase(_chip, _op.a, _op.b, offset);
					f.get(_chip.acc);
					encode24();
					f.i64Store32(offset);
					break;
				}
			case FlatOpKind::iramWriteLatch:
				{
					uint32_t offset = 0;
					pushIramBase(_chip, _op.a, _op.b, offset);
					f.get(_chip.eram);
					f.i64Const(0xffffff);
					f.i64And();
					f.i64Store32(offset);
					break;
				}
			case FlatOpKind::iramRead:
				{
					uint32_t offset = 0;
					pushIramBase(_chip, _op.a, _op.b, offset);
					f.i64Load32U(offset);
					signExtend(24);
					f.set(_chip.iram);
					break;
				}
			case FlatOpKind::iramReadParam:
				f.get(_chip.state);
				f.get(_chip.state);
				f.i32Load(off(offsetof(DspState, iram3) + _op.b * 4));
				f.i32Const(10);
				f.i32ShrU();
				f.i32Store16(off(offsetof(DspState, iram3ParameterLatch)));
				break;
			case FlatOpKind::emitA:
				f.get(_chip.state);
				f.get(_chip.procBank);
				f.i64Load32U(_op.b * 4u);
				signExtend(24);
				f.i64Store32(off(offsetof(DspState, serialOutput) + _op.a * 4));
				break;
			case FlatOpKind::emitBcd:
				if (_op.c != 0xff)
				{
					f.get(_chip.state);
					f.get(_chip.procBank);
					f.i64Load32U(_op.b * 4u);
					signExtend(24);
					f.i64Store32(off(offsetof(DspState, serialOutput) + (1 + _op.a) * dsp::nSerialWords * 4 + _op.c * 4));
				}
				break;
			case FlatOpKind::pins:
				// A falling OUTP0 latches bit 2; the levels replace bits 1:0.
				f.get(_chip.state);
				f.get(_chip.state);
				f.i32Load8U(off(offsetof(DspState, outputPins)));
				f.set(m_i[0]);
				f.get(m_i[0]);
				if ((_op.a & 1) == 0)
				{
					f.get(m_i[0]);
					f.i32Const(1);
					f.i32And();
					f.i32Const(2);
					f.i32Shl();
					f.i32Or();
				}
				f.i32Const(4);
				f.i32And();
				f.i32Const(_op.a);
				f.i32Or();
				f.i32Store8(off(offsetof(DspState, outputPins)));
				break;
			case FlatOpKind::consumeA:
				emitConsume(_chip, 0);
				break;
			case FlatOpKind::consumeB:
				emitConsume(_chip, 1);
				break;
			case FlatOpKind::mulCapture:
				pushMultiplySource(_chip, static_cast<DspMultiplyInput>(_op.a));
				f.set(m_cap[0]);
				if (_op.b == _op.a)
					f.get(m_cap[0]);
				else
					pushMultiplySource(_chip, static_cast<DspMultiplyInput>(_op.b));
				f.set(m_cap[1]);
				break;
			case FlatOpKind::factorCapture:
				{
					const auto kind = static_cast<DspMultiplyFactor>(_op.a);
					if (kind != DspMultiplyFactor::iram3Parameter)
					{
						f.get(_chip.acc);
						saturate(24);
					}
					switch (kind)
					{
					case DspMultiplyFactor::accLow12Shl3:
						f.i64Const(0xfff);
						f.i64And();
						f.i64Const(3);
						f.i64Shl();
						break;
					case DspMultiplyFactor::accBits22to8:
						f.i64Const(8);
						f.i64ShrU();
						f.i64Const(0x7fff);
						f.i64And();
						break;
					case DspMultiplyFactor::accShr8:
						f.i64Const(8);
						f.i64ShrS();
						break;
					default:
						f.get(_chip.state);
						f.i64Load16S(off(offsetof(DspState, iram3ParameterLatch)));
						break;
					}
					f.set(m_cap[2]);
					if (_op.b)
					{
						f.i64Const(kind >= DspMultiplyFactor::accShr8 ? 0x7fff - 0x8000 : 0x7fff);
						f.get(m_cap[2]);
						f.i64Sub();
						f.set(m_cap[2]);
					}
					break;
				}
			case FlatOpKind::alu:
				{
					const auto kind = static_cast<DspAlu>(_op.a);
					if (kind >= DspAlu::andImm && kind <= DspAlu::negAccPlusImm)
					{
						pushParam(_chip, _op.index);
						f.set(m_t[0]);
					}
					emitAlu(_chip, kind);
					break;
				}
			case FlatOpKind::parallel:
				emitAlu(_chip, static_cast<DspAlu>(_op.a), false);
				emitTransform(_chip, static_cast<DspTransform>(_op.b), _op.c != 0);
				break;
			case FlatOpKind::mulCoefficient:
				f.get(m_cap[0]);
				pushParam(_chip, _op.index);
				f.i64Mul();
				f.set(m_t[0]);
				emitNegativeFraction(_chip, m_t[0], 13);
				f.get(m_t[0]);
				divideTruncating(13);
				saturate(29);
				f.set(_chip.mul);
				f.get(m_cap[1]);
				f.set(_chip.fb);
				break;
			case FlatOpKind::mulFactor:
				f.get(m_cap[0]);
				f.get(m_cap[2]);
				f.i64Mul();
				if (_op.a != 0)
				{
					f.i64Const(_op.a);
					f.i64Shl();
				}
				f.set(m_t[0]);
				emitNegativeFraction(_chip, m_t[0], 15);
				f.get(m_t[0]);
				divideTruncating(15);
				saturate(29);
				f.set(_chip.mul);
				f.get(m_cap[1]);
				f.set(_chip.fb);
				break;
			case FlatOpKind::branch:
				if (m_jumpsMode)
				{
					// Emitted after the slot's remaining ops and the frame-end check by emitJumpsProgram.
					m_pendingBranch = true;
					m_pendingCondition = static_cast<DspBranch>(_op.a);
					m_pendingTarget = _op.index;
					break;
				}
				m_lastError = "branch outside the jumps form";
				break;
			default:
				// The runtime-counter ops belong to dynamic programs, which emit() turns down.
				m_lastError = "dynamic op in a static program";
				break;
			}
		}

		wasmJit::HostFunction m_host;
		DspJitRun m_run = nullptr;
		size_t m_codeSize = 0;
		const char* m_lastError = "";

		// Per-emit state.
		F* m_f = nullptr;
		std::vector<Chip> m_chips;
		Local m_frame;
		Local m_t[4];   // i64 temporaries; t3 belongs to the arithmetic helpers
		Local m_cap[3]; // multiply captures: input, feedback source, factor
		Local m_i[2];   // i32 temporaries
		bool m_jumpsMode = false;
		bool m_pendingBranch = false;
		DspBranch m_pendingCondition = DspBranch::never;
		uint16_t m_pendingTarget = 0;
	};
} // namespace xpLib
