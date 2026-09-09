#pragma once

// arm64 backend of the XP DSP JIT. One straight-line function per frame: the slots of a static program are
// emitted in cycle order with the mixer deposits of each cycle ahead of the slot that runs in it; a linked
// partner's slots are interleaved cycle by cycle with the SDOA-to-SDIA word handoff after both slots. A chip
// with a reachable branch is emitted as one body per PRAM slot, entered from the cycle skeleton through a
// jump table on its program counter and returning to the cycle's continuation.
//
// The datapath latches live in registers for the whole frame and are stored back in the epilogue, so the
// interpreter can take over at any frame boundary. The generated code reads only the DspJitFrame it is
// handed, the two DspState/DspParams pairs it points to and the mixer frames; it never calls out.

#include <asmjit/asmjit.h>
#include <asmjit/a64.h>

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
		DspJitBackend() = default;
		~DspJitBackend() { release(); }
		DspJitBackend(const DspJitBackend&) = delete;
		DspJitBackend& operator=(const DspJitBackend&) = delete;

		DspJitRun run() const { return m_run; }

		void release()
		{
			if (!m_run)
				return;
			m_runtime.release(m_run);
			m_run = nullptr;
		}

		// Emits the frame function for one lowered program, or for a lockstep-linked pair.
		bool compile(const FlatProgram& _a, const FlatProgram* _b)
		{
			namespace a64 = asmjit::a64;
			release();

			asmjit::CodeHolder code;
			code.init(m_runtime.environment());
			a64::Builder builder(&code);
			m_asm = &builder;
			m_chips.clear();
			m_chips.push_back(Chip{&_a, 0});
			if (_b != nullptr)
				m_chips.push_back(Chip{_b, 1});
			if (!allocateRegisters())
				return false;

			auto& a = *m_asm;
			// Prologue: the frame pointer, the link register and every callee-saved register are preserved.
			a.stp(a64::x29, a64::x30, a64::Mem(a64::sp, -16).pre());
			a.stp(a64::x19, a64::x20, a64::Mem(a64::sp, -16).pre());
			a.stp(a64::x21, a64::x22, a64::Mem(a64::sp, -16).pre());
			a.stp(a64::x23, a64::x24, a64::Mem(a64::sp, -16).pre());
			a.stp(a64::x25, a64::x26, a64::Mem(a64::sp, -16).pre());
			a.stp(a64::x27, a64::x28, a64::Mem(a64::sp, -16).pre());

			for (auto& chip : m_chips)
				emitChipEntry(chip);
			a.mov(m_c24, 0x7fffff);
			a.mov(m_c29, 0x0fffffff);

			if (m_chips.size() == 1 && m_chips[0].program->jumps)
				emitJumpsProgram(m_chips[0]);
			else
			{
				for (auto& chip : m_chips)
					if (chip.program->jumps)
						return false;
				emitCycles();
			}
			for (auto& chip : m_chips)
				emitChipExit(chip);

			a.ldp(a64::x27, a64::x28, a64::Mem(a64::sp, 16).post(0));
			a.ldp(a64::x25, a64::x26, a64::Mem(a64::sp, 16).post(0));
			a.ldp(a64::x23, a64::x24, a64::Mem(a64::sp, 16).post(0));
			a.ldp(a64::x21, a64::x22, a64::Mem(a64::sp, 16).post(0));
			a.ldp(a64::x19, a64::x20, a64::Mem(a64::sp, 16).post(0));
			a.ldp(a64::x29, a64::x30, a64::Mem(a64::sp, 16).post(0));
			a.ret(a64::x30);

			for (auto& chip : m_chips)
				if (chip.dynamic)
					emitBodies(chip);

			m_lastError = a.finalize();
			if (m_lastError != asmjit::kErrorOk)
				return false;
			m_lastError = m_runtime.add(&m_run, &code);
			if (m_lastError != asmjit::kErrorOk)
				return false;
			return true;
		}

	private:
		using Gp = asmjit::a64::GpX;
		using Mem = asmjit::a64::Mem;
		using Cond = asmjit::a64::CondCode;

		// A value that lives in a register for the frame, or in memory reached through a base register.
		struct Loc
		{
			Gp reg{};
			bool inReg = false;
			Gp base{};
			int32_t offset = 0;
			uint8_t size = 8;
			bool isSigned = true;
		};

		struct Chip
		{
			const FlatProgram* program;
			int index;
			bool dynamic = false;
			uint16_t slots = 0;
			bool peerListens = false;
			Gp state, params, mixBank, procBank;
			Gp skip; // jumps form: slots skipped so far this frame
			Loc acc, iram, mul, fb, eram, nodeA;
			Loc wv, mask, eramPos, mixer, nodeB;
			std::vector<asmjit::Label> bodies;
			asmjit::Label idle, table;
		};

		static constexpr Gp frameReg() { return asmjit::a64::x0; }

		static int32_t stateOffset(const size_t _offset) { return static_cast<int32_t>(_offset); }

		Mem st(const Chip& _chip, const size_t _offset) const
		{
			return asmjit::a64::ptr(_chip.state, stateOffset(_offset));
		}
		Mem fr(const size_t _offset) const { return asmjit::a64::ptr(frameReg(), stateOffset(_offset)); }

		// ---- register allocation ----
		bool allocateRegisters()
		{
			namespace a64 = asmjit::a64;
			m_pool.clear();
			for (int r = 19; r <= 30; ++r)
				m_pool.push_back(Gp(static_cast<uint32_t>(r)));
			for (int r = 1; r <= 17; ++r)
				m_pool.push_back(Gp(static_cast<uint32_t>(r)));
			m_poolNext = 0;

			const auto take = [this](Gp& _reg)
			{
				if (m_poolNext >= m_pool.size())
					return false;
				_reg = m_pool[m_poolNext++];
				return true;
			};
			const auto latch = [&](Loc& _loc, const Chip& _chip, const size_t _offset, const uint8_t _size,
								   const bool _signed) -> bool
			{
				_loc.inReg = take(_loc.reg);
				_loc.base = _chip.state;
				_loc.offset = stateOffset(_offset);
				_loc.size = _size;
				_loc.isSigned = _signed;
				return _loc.inReg;
			};

			for (auto& chip : m_chips)
			{
				chip.dynamic = chip.program->dynamic;
				chip.slots = chip.program->config.executionSlots;
				if (!take(chip.state) || !take(chip.params) || !take(chip.mixBank) || !take(chip.procBank))
					return false;
			}
			for (auto& chip : m_chips)
			{
				if (!latch(chip.acc, chip, offsetof(DspState, accumulator), 8, true) ||
					!latch(chip.iram, chip, offsetof(DspState, iramReadLatch), 8, true) ||
					!latch(chip.mul, chip, offsetof(DspState, multiplyResultLatch), 8, true) ||
					!latch(chip.fb, chip, offsetof(DspState, multiplyFeedbackLatch), 8, true) ||
					!latch(chip.eram, chip, offsetof(DspState, eramReadLatch), 8, true) ||
					!latch(chip.nodeA, chip, offsetof(DspState, serialInputNode), 8, true))
					return false;
			}
			for (auto& t : m_t)
				if (!take(t))
					return false;
			for (auto& c : m_cap)
				if (!take(c))
					return false;
			if (!take(m_c24) || !take(m_c29))
				return false;
			for (auto& chip : m_chips)
				if (chip.program->jumps && !take(chip.skip))
					return false;
			// Extras take whatever is left, in order of how often they are used.
			for (auto& chip : m_chips)
			{
				latch(chip.eramPos, chip, offsetof(DspState, eramPos), 4, false);
				latch(chip.mask, chip, offsetof(DspState, mixerInitialized), 8, false);
				chip.mixer.inReg = take(chip.mixer.reg);
				chip.mixer.base = frameReg();
				chip.mixer.offset = stateOffset(offsetof(DspJitFrame, mixer) + chip.index * sizeof(void*));
				chip.mixer.size = 8;
				latch(chip.wv, chip, offsetof(DspState, eramPendingWriteValue), 8, true);
				latch(chip.nodeB, chip, offsetof(DspState, serialInputNode) + sizeof(int64_t), 8, true);
			}
			if (m_chips.size() == 2)
			{
				m_chips[0].peerListens = m_chips[1].program->config.serialInputEnabled != 0;
				m_chips[1].peerListens = m_chips[0].program->config.serialInputEnabled != 0;
			}
			return true;
		}

		// ---- value access ----
		Gp get(const Loc& _loc, const Gp& _tmp)
		{
			if (_loc.inReg)
				return _loc.reg;
			const auto mem = asmjit::a64::ptr(_loc.base, _loc.offset);
			switch (_loc.size)
			{
			case 8:
				m_asm->ldr(_tmp, mem);
				break;
			case 4:
				if (_loc.isSigned)
					m_asm->ldrsw(_tmp, mem);
				else
					m_asm->ldr(_tmp.w(), mem);
				break;
			case 2:
				if (_loc.isSigned)
					m_asm->ldrsh(_tmp, mem);
				else
					m_asm->ldrh(_tmp.w(), mem);
				break;
			default:
				m_asm->ldrb(_tmp.w(), mem);
				break;
			}
			return _tmp;
		}

		void put(const Loc& _loc, const Gp& _src)
		{
			if (_loc.inReg)
			{
				if (_loc.reg.id() != _src.id())
					m_asm->mov(_loc.reg, _src);
				return;
			}
			const auto mem = asmjit::a64::ptr(_loc.base, _loc.offset);
			switch (_loc.size)
			{
			case 8:
				m_asm->str(_src, mem);
				break;
			case 4:
				m_asm->str(_src.w(), mem);
				break;
			case 2:
				m_asm->strh(_src.w(), mem);
				break;
			default:
				m_asm->strb(_src.w(), mem);
				break;
			}
		}

		// ---- arithmetic helpers ----
		void sat24(const Gp& _dst, const Gp& _src)
		{
			m_asm->cmn(_src, m_c24);
			m_asm->csinv(_dst, _src, m_c24, Cond::kGE);
			m_asm->cmp(_dst, m_c24);
			m_asm->csel(_dst, _dst, m_c24, Cond::kLE);
		}

		void sat29(const Gp& _dst, const Gp& _src)
		{
			m_asm->cmn(_src, m_c29);
			m_asm->csinv(_dst, _src, m_c29, Cond::kGE);
			m_asm->cmp(_dst, m_c29);
			m_asm->csel(_dst, _dst, m_c29, Cond::kLE);
		}

		// dst = src / 2^shift, truncating toward zero.
		void divideTruncating(const Gp& _dst, const Gp& _src, const unsigned _shift, const Gp& _tmp)
		{
			m_asm->asr(_tmp, _src, 63);
			m_asm->add(_dst, _src, _tmp, asmjit::a64::lsr(64 - _shift));
			m_asm->asr(_dst, _dst, _shift);
		}

		// Stores whether a multiply numerator is negative with a nonzero remainder below 2^_shift; t1 is
		// clobbered.
		void emitNegativeFraction(Chip& _chip, const Gp& _numerator, const unsigned _shift)
		{
			namespace a64 = asmjit::a64;
			m_asm->tst(_numerator, (int64_t{1} << _shift) - 1);
			m_asm->cset(m_t[1], Cond::kNE);
			m_asm->and_(m_t[1], m_t[1], _numerator, a64::lsr(63));
			m_asm->strb(m_t[1].w(), st(_chip, offsetof(DspState, multiplyNegativeFraction)));
		}

		// The 24-bit memory image of a value: saturated, masked.
		void encode24(const Gp& _dst, const Gp& _src)
		{
			sat24(_dst, _src);
			m_asm->and_(_dst, _dst, 0xffffff);
		}

		Mem iramCell(const Chip& _chip, const uint8_t _bank, const uint8_t _index) const
		{
			namespace a64 = asmjit::a64;
			const auto byteOffset = static_cast<int32_t>(_index) * 4;
			switch (static_cast<DspIramBank>(_bank))
			{
			case DspIramBank::mixer:
				return a64::ptr(_chip.mixBank, byteOffset);
			case DspIramBank::processing:
				return a64::ptr(_chip.procBank, byteOffset);
			case DspIramBank::direct1:
				return st(_chip, offsetof(DspState, iram1) + byteOffset);
			case DspIramBank::direct2:
				return st(_chip, offsetof(DspState, iram2) + byteOffset);
			default:
				return st(_chip, offsetof(DspState, iram3) + byteOffset);
			}
		}

		// t = &eram[0]; index register in _offset: address = (eramPos + offset) & 0xffff.
		void eramAddress(const Chip& _chip, const Gp& _offset, const Gp& _base, const Gp& _tmp)
		{
			m_asm->add(_offset, _offset, get(_chip.eramPos, _tmp));
			m_asm->and_(_offset, _offset, 0xffff);
			m_asm->add(_base, _chip.state, stateOffset(offsetof(DspState, eram)));
		}

		void eramLoad(const Gp& _dst, const Gp& _base, const Gp& _index)
		{
			m_asm->ldr(_dst.w(), asmjit::a64::ptr(_base, _index, asmjit::a64::lsl(2)));
			m_asm->sbfx(_dst, _dst, 0, 24);
		}

		void loadParam(const Chip& _chip, const Gp& _dst, const uint16_t _slot)
		{
			m_asm->ldrsw(_dst, asmjit::a64::ptr(_chip.params, stateOffset(offsetof(DspParams, param) + _slot * 4)));
		}

		void loadEramOffset(const Chip& _chip, const Gp& _dst, const uint16_t _slot)
		{
			m_asm->ldrh(_dst.w(),
						asmjit::a64::ptr(_chip.params, stateOffset(offsetof(DspParams, eramOffset) + _slot * 2)));
		}

		static size_t queueOffset(const int _entry, const bool _value)
		{
			return offsetof(DspState, pendingEramReads) + _entry * sizeof(DspState::PendingEramRead) +
				(_value ? offsetof(DspState::PendingEramRead, value) : offsetof(DspState::PendingEramRead, countdown));
		}

		// ---- prologue / epilogue ----
		void emitChipEntry(Chip& _chip)
		{
			namespace a64 = asmjit::a64;
			auto& a = *m_asm;
			const auto i = static_cast<size_t>(_chip.index);
			a.ldr(_chip.state, fr(offsetof(DspJitFrame, state) + i * sizeof(void*)));
			a.ldr(_chip.params, fr(offsetof(DspJitFrame, params) + i * sizeof(void*)));
			a.ldr(_chip.mixBank, fr(offsetof(DspJitFrame, mixerBank) + i * sizeof(void*)));
			a.ldr(_chip.procBank, fr(offsetof(DspJitFrame, processingBank) + i * sizeof(void*)));
			for (const Loc* loc : {&_chip.acc, &_chip.iram, &_chip.mul, &_chip.fb, &_chip.eram, &_chip.nodeA, &_chip.eramPos,
								   &_chip.mask, &_chip.mixer, &_chip.wv, &_chip.nodeB})
				if (loc->inReg)
					get(Loc{Gp{}, false, loc->base, loc->offset, loc->size, loc->isSigned}, loc->reg);
			if (_chip.dynamic)
				a.str(a64::wzr, fr(offsetof(DspJitFrame, pc) + i * sizeof(uint32_t)));
			a.strb(a64::wzr, fr(offsetof(DspJitFrame, linkFlag) + i));
		}

		void emitChipExit(Chip& _chip)
		{
			namespace a64 = asmjit::a64;
			auto& a = *m_asm;
			for (const Loc* loc : {&_chip.acc, &_chip.iram, &_chip.mul, &_chip.fb, &_chip.eram, &_chip.nodeA, &_chip.mask,
								   &_chip.wv, &_chip.nodeB})
				if (loc->inReg)
					put(Loc{Gp{}, false, loc->base, loc->offset, loc->size, loc->isSigned}, loc->reg);
			if (_chip.dynamic)
				return;
			const auto& carry = _chip.program->carry;
			a.mov(m_t[0], carry.outputWordPosition);
			a.str(m_t[0].w(), st(_chip, offsetof(DspState, outputWordPosition)));
			a.mov(m_t[0], carry.dacPortPosition);
			a.strb(m_t[0].w(), st(_chip, offsetof(DspState, dacPortPosition)));
			for (size_t bus = 0; bus < dsp::nSerialBuses; ++bus)
			{
				a.mov(m_t[0], carry.serialOutputCount[bus]);
				a.str(m_t[0].w(), st(_chip, offsetof(DspState, serialOutputCount) + bus * 4));
			}
			if (!carry.staticRegion)
				return;
			a.mov(m_t[0], carry.prefixPendingAtEnd);
			a.strb(m_t[0].w(), st(_chip, offsetof(DspState, eramPrefixPending)));
			if (carry.hasArm)
			{
				a.mov(m_t[0], carry.lastArmIsWrite);
				a.strb(m_t[0].w(), st(_chip, offsetof(DspState, eramPendingWrite)));
				a.ldrb(m_t[0].w(), a64::ptr(_chip.params, stateOffset(offsetof(DspParams, eramOffsetHigh) + carry.lastArmSlot)));
				a.strb(m_t[0].w(), st(_chip, offsetof(DspState, eramOffsetHigh)));
			}
			for (int e = 0; e < 2; ++e)
			{
				const auto& entry = carry.queue[e];
				a.mov(m_t[0], static_cast<int64_t>(entry.countdown));
				a.str(m_t[0].w(), st(_chip, queueOffset(e, false)));
				if (entry.countdown < 0 || entry.indexed)
					continue;
				loadEramOffset(_chip, m_t[0], entry.slot);
				eramAddress(_chip, m_t[0], m_t[1], m_t[2]);
				eramLoad(m_t[2], m_t[1], m_t[0]);
				a.str(m_t[2].w(), st(_chip, queueOffset(e, true)));
			}
		}

		// The cycle-major skeleton: static slots inline, dynamic chips dispatched, the pair handoff after both.
		void emitCycles()
		{
			auto& a = *m_asm;
			size_t cycles = 0;
			for (auto& chip : m_chips)
			{
				if (chip.dynamic)
				{
					chip.bodies.resize(dsp::nProgramSlots);
					for (auto& label : chip.bodies)
						label = a.newLabel();
					chip.idle = a.newLabel();
					chip.table = a.newLabel();
				}
				cycles = std::max<size_t>(cycles, chip.slots);
			}
			for (size_t cycle = 0; cycle < cycles; ++cycle)
			{
				for (auto& chip : m_chips)
					if (cycle < chip.slots)
					{
						emitDeposits(chip, cycle);
						if (chip.dynamic)
							emitDispatch(chip, cycle);
						else
							emitSlot(chip, cycle);
					}
				if (m_chips.size() == 2)
					emitExchange(cycle);
			}
		}

		// The jumps form: straight-line slots with direct forward jumps. The skip register accumulates the
		// slots jumped over; the frame ends after the slot whose number minus the skips is the last cycle.
		void emitJumpsProgram(Chip& _chip)
		{
			auto& a = *m_asm;
			const auto& flat = *_chip.program;
			const size_t budget = flat.config.executionSlots;
			std::vector<asmjit::Label> targets(flat.slotsLowered);
			for (const auto& op : flat.ops)
				if (op.kind == FlatOpKind::branch && op.index < flat.slotsLowered && !targets[op.index].isValid())
					targets[op.index] = a.newLabel();
			m_jumpsExit = a.newLabel();
			m_jumpsMode = true;
			a.mov(_chip.skip, 0);
			for (size_t pc = 0; pc < flat.slotsLowered; ++pc)
			{
				if (targets[pc].isValid())
					a.bind(targets[pc]);
				m_pendingBranch = false;
				emitSlot(_chip, pc);
				if (pc + 1 >= budget)
				{
					a.cmp(_chip.skip, pc + 1 - budget);
					a.b(Cond::kEQ, m_jumpsExit);
				}
				if (!m_pendingBranch || m_pendingTarget >= flat.slotsLowered || m_pendingTarget <= pc + 1)
					continue;
				const auto skipped = static_cast<uint32_t>(m_pendingTarget - pc - 1);
				if (m_pendingCondition == DspBranch::always)
				{
					a.add(_chip.skip, _chip.skip, skipped);
					a.b(targets[m_pendingTarget]);
					continue;
				}
				const auto fallthrough = a.newLabel();
				Cond notTaken = Cond::kNE;
				switch (m_pendingCondition)
				{
				case DspBranch::eq0: notTaken = Cond::kNE; break;
				case DspBranch::ne0: notTaken = Cond::kEQ; break;
				case DspBranch::ge0: notTaken = Cond::kLT; break;
				case DspBranch::lt0: notTaken = Cond::kGE; break;
				case DspBranch::gt0: notTaken = Cond::kLE; break;
				default: notTaken = Cond::kGT; break;
				}
				a.cmp(get(_chip.acc, m_t[0]), 0);
				a.b(notTaken, fallthrough);
				a.add(_chip.skip, _chip.skip, skipped);
				a.b(targets[m_pendingTarget]);
				a.bind(fallthrough);
			}
			a.bind(m_jumpsExit);
			m_jumpsMode = false;
		}

		// ---- mixer deposits ----
		void emitDeposits(Chip& _chip, const size_t _cycle)
		{
			namespace a64 = asmjit::a64;
			auto& a = *m_asm;
			const auto phase = _cycle & 3;
			if (phase == 1)
				return;
			const auto voice = _cycle >> 2;
			// A null mixer frame means the deposits were hoisted ahead of the frame, or there are none.
			const auto none = a.newLabel();
			const Gp mixer = get(_chip.mixer, m_t[3]);
			a.cbz(mixer, none);
			const Gp mask = _chip.mask.inReg ? _chip.mask.reg : m_cap[0];
			if (!_chip.mask.inReg)
				get(_chip.mask, mask);
			const auto deposit = [&](const size_t _send)
			{
				const auto offset = static_cast<int32_t>((voice * 4 + _send) * sizeof(DspMixerSend));
				const auto skip = a.newLabel();
				const auto load = a.newLabel();
				const auto add = a.newLabel();
				const Gp dest = m_t[0];
				const Gp contribution = m_t[1];
				const Gp value = m_t[2];
				a.ldr(dest, a64::ptr(mixer, offset + static_cast<int32_t>(offsetof(DspMixerSend, destination))));
				a.cmp(dest, dsp::nIramSlots);
				a.b(Cond::kHS, skip);
				a.ldr(contribution, a64::ptr(mixer, offset + static_cast<int32_t>(offsetof(DspMixerSend, contribution))));
				a.lsr(value, mask, dest);
				a.tbnz(value, 0, load);
				// First send of the frame: the cell starts from zero.
				a.mov(value, 1);
				a.lsl(value, value, dest);
				a.orr(mask, mask, value);
				a.mov(value, 0);
				a.b(add);
				a.bind(load);
				a.ldr(value.w(), a64::ptr(_chip.mixBank, dest, a64::lsl(2)));
				a.sbfx(value, value, 0, 24);
				a.bind(add);
				a.add(value, value, contribution);
				encode24(value, value);
				a.str(value.w(), a64::ptr(_chip.mixBank, dest, a64::lsl(2)));
				a.bind(skip);
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
			if (!_chip.mask.inReg)
				put(_chip.mask, mask);
			a.bind(none);
		}

		// ---- dynamic chips: dispatch and bodies ----
		void emitDispatch(Chip& _chip, const size_t)
		{
			namespace a64 = asmjit::a64;
			auto& a = *m_asm;
			const auto i = static_cast<size_t>(_chip.index);
			const auto continuation = a.newLabel();
			a.ldr(m_t[0].w(), fr(offsetof(DspJitFrame, pc) + i * sizeof(uint32_t)));
			a.mov(m_t[1], dsp::nProgramSlots);
			a.cmp(m_t[0], m_t[1]);
			a.csel(m_t[0], m_t[0], m_t[1], Cond::kLO);
			a.adr(m_t[1], _chip.table);
			a.add(m_t[1], m_t[1], m_t[0], a64::lsl(2));
			a.adr(m_t[2], continuation);
			a.str(m_t[2], fr(offsetof(DspJitFrame, returnAddress)));
			a.br(m_t[1]);
			a.bind(continuation);
		}

		void emitBodies(Chip& _chip)
		{
			auto& a = *m_asm;
			a.bind(_chip.table);
			for (size_t pc = 0; pc < dsp::nProgramSlots; ++pc)
				a.b(_chip.bodies[pc]);
			a.b(_chip.idle);
			for (size_t pc = 0; pc < dsp::nProgramSlots; ++pc)
			{
				a.bind(_chip.bodies[pc]);
				m_pcNextWritten = false;
				emitSlot(_chip, pc);
				if (!m_pcNextWritten)
				{
					a.mov(m_t[0], pc + 1);
					a.str(m_t[0].w(), fr(offsetof(DspJitFrame, pc) + _chip.index * sizeof(uint32_t)));
				}
				a.ldr(m_t[0], fr(offsetof(DspJitFrame, returnAddress)));
				a.br(m_t[0]);
			}
			a.bind(_chip.idle);
			a.ldr(m_t[0], fr(offsetof(DspJitFrame, returnAddress)));
			a.br(m_t[0]);
		}

		// ---- the lockstep word handoff ----
		void emitExchange(const size_t _cycle)
		{
			namespace a64 = asmjit::a64;
			auto& a = *m_asm;
			for (size_t from = 0; from < 2; ++from)
			{
				auto& source = m_chips[from];
				auto& peer = m_chips[1 - from];
				if (!source.peerListens || _cycle >= source.slots)
					continue;
				if (!source.dynamic)
				{
					const auto& flat = *source.program;
					for (size_t i = flat.slotToOp[_cycle]; i < flat.slotToOp[_cycle + 1]; ++i)
					{
						const auto& op = flat.ops[i];
						if (op.kind != FlatOpKind::emitA)
							continue;
						a.ldrsw(m_t[0], st(source, offsetof(DspState, serialOutput) + op.a * 4));
						put(peer.nodeA, m_t[0]);
					}
					continue;
				}
				const auto skip = a.newLabel();
				a.ldrb(m_t[0].w(), fr(offsetof(DspJitFrame, linkFlag) + from));
				a.cbz(m_t[0], skip);
				a.ldrsw(m_t[0], fr(offsetof(DspJitFrame, linkWord) + from * sizeof(int32_t)));
				put(peer.nodeA, m_t[0]);
				a.strb(a64::wzr, fr(offsetof(DspJitFrame, linkFlag) + from));
				a.bind(skip);
			}
		}

		// ---- one slot ----
		void emitSlot(Chip& _chip, const size_t _pc)
		{
			const auto& flat = *_chip.program;
			for (size_t i = flat.slotToOp[_pc]; i < flat.slotToOp[_pc + 1]; ++i)
				emitOp(_chip, flat.ops[i], _pc);
		}

		void multiplySource(Chip& _chip, const Gp& _dst, const DspMultiplyInput _input)
		{
			switch (_input)
			{
			case DspMultiplyInput::feedbackLatch:
				put(Loc{_dst, true}, get(_chip.fb, _dst));
				break;
			case DspMultiplyInput::accumulatorSat24:
				sat24(_dst, get(_chip.acc, _dst));
				break;
			case DspMultiplyInput::iramReadLatch:
				put(Loc{_dst, true}, get(_chip.iram, _dst));
				break;
			case DspMultiplyInput::eramReadLatch:
				put(Loc{_dst, true}, get(_chip.eram, _dst));
				break;
			case DspMultiplyInput::serialNodeA:
				put(Loc{_dst, true}, get(_chip.nodeA, _dst));
				break;
			default:
				put(Loc{_dst, true}, get(_chip.nodeB, _dst));
				break;
			}
		}

		// acc = f(acc, iram, mul, param). A primary op wraps the result to 29 bits here; a parallel op keeps
		// the raw result for its transform, which wraps afterwards (an absolute value of a result beyond 28
		// bits differs between the two orders).
		void emitAlu(Chip& _chip, const DspAlu _alu, const Gp& _param, const bool _wrap = true)
		{
			namespace a64 = asmjit::a64;
			auto& a = *m_asm;
			const Gp acc = get(_chip.acc, m_t[3]);
			const Gp iram = get(_chip.iram, m_t[1]);
			const Gp mul = get(_chip.mul, m_t[2]);
			switch (_alu)
			{
			case DspAlu::hold:
				return;
			case DspAlu::accPlusIram:
				a.add(acc, acc, iram);
				break;
			case DspAlu::accPlusMul:
				a.add(acc, acc, mul);
				break;
			case DspAlu::iram:
				a.mov(acc, iram);
				break;
			case DspAlu::mul:
				a.mov(acc, mul);
				break;
			case DspAlu::negAcc:
				a.neg(acc, acc);
				break;
			case DspAlu::iramMinusAcc:
				a.sub(acc, iram, acc);
				break;
			case DspAlu::mulMinusAcc:
				a.sub(acc, mul, acc);
				break;
			case DspAlu::iramPlusMul:
				a.add(acc, iram, mul);
				break;
			case DspAlu::minAccIram:
				a.cmp(acc, iram);
				a.csel(acc, acc, iram, Cond::kLT);
				break;
			case DspAlu::maxAccIram:
				a.cmp(acc, iram);
				a.csel(acc, acc, iram, Cond::kGT);
				break;
			case DspAlu::accPlusMulShr13:
				a.add(acc, acc, mul, a64::asr(13));
				break;
			case DspAlu::mulShr13:
				a.asr(acc, mul, 13);
				break;
			case DspAlu::andImm:
				a.and_(acc, acc, 0x1fffffff);
				a.and_(acc, acc, _param);
				break;
			case DspAlu::orImm:
				a.and_(acc, acc, 0x1fffffff);
				a.orr(acc, acc, _param);
				break;
			case DspAlu::xorImm:
				a.and_(acc, acc, 0x1fffffff);
				a.eor(acc, acc, _param);
				break;
			case DspAlu::minImm:
				a.cmp(acc, _param);
				a.csel(acc, acc, _param, Cond::kLT);
				break;
			case DspAlu::maxImm:
				a.cmp(acc, _param);
				a.csel(acc, acc, _param, Cond::kGT);
				break;
			case DspAlu::sameSignMinElseMax:
			case DspAlu::sameSignMaxElseMin:
				{
					// Signs differ when the xor of the operands is negative.
					const Gp minimum = m_t[1];
					const Gp maximum = m_t[2];
					a.cmp(acc, _param);
					a.csel(minimum, acc, _param, Cond::kLT);
					a.csel(maximum, acc, _param, Cond::kGT);
					a.eor(_param, acc, _param);
					a.cmp(_param, 0);
					if (_alu == DspAlu::sameSignMinElseMax)
						a.csel(acc, maximum, minimum, Cond::kLT);
					else
						a.csel(acc, minimum, maximum, Cond::kLT);
					break;
				}
			case DspAlu::accPlusImm:
				a.add(acc, acc, _param);
				break;
			case DspAlu::iramPlusImm:
				a.add(acc, iram, _param);
				break;
			case DspAlu::mulPlusImm:
				a.add(acc, mul, _param);
				break;
			case DspAlu::negAccPlusImm:
				a.sub(acc, _param, acc);
				break;
			case DspAlu::accPlusMulPlusIram:
				a.add(acc, acc, mul);
				a.add(acc, acc, iram);
				break;
			case DspAlu::iramPlusMulMinusAcc:
				a.add(_param, iram, mul);
				a.sub(acc, _param, acc);
				break;
			// The parallel D-F functions also consume the discarded negative fraction of the preceding multiply.
			case DspAlu::accPlusMulMinusIram:
				a.add(acc, acc, mul);
				a.sub(acc, acc, iram);
				a.ldrb(_param.w(), st(_chip, offsetof(DspState, multiplyNegativeFraction)));
				a.sub(acc, acc, _param);
				break;
			case DspAlu::mulMinusIramMinusAcc:
				a.sub(_param, mul, iram);
				a.sub(acc, _param, acc);
				a.ldrb(_param.w(), st(_chip, offsetof(DspState, multiplyNegativeFraction)));
				a.sub(acc, acc, _param, a64::lsl(1));
				break;
			case DspAlu::mulMinusIram:
				a.sub(acc, mul, iram);
				a.ldrb(_param.w(), st(_chip, offsetof(DspState, multiplyNegativeFraction)));
				a.sub(acc, acc, _param, a64::lsl(1));
				break;
			}
			if (_wrap)
				a.sbfx(acc, acc, 0, 29);
			put(_chip.acc, acc);
		}

		void emitTransform(Chip& _chip, const DspTransform _transform, const bool _sext24)
		{
			namespace a64 = asmjit::a64;
			auto& a = *m_asm;
			const Gp acc = get(_chip.acc, m_t[3]);
			bool narrowed = false;
			switch (_transform)
			{
			case DspTransform::absolute:
				a.cmp(acc, 0);
				a.cneg(acc, acc, Cond::kLT);
				break;
			case DspTransform::sext24:
				a.sbfx(acc, acc, 0, 24);
				narrowed = true;
				break;
			case DspTransform::lfsr:
				a.asr(m_t[0], acc, 23);
				a.eor(m_t[0], m_t[0], acc, a64::asr(6));
				a.eor(m_t[0], m_t[0], acc, a64::asr(1));
				a.and_(m_t[0], m_t[0], 1);
				a.orr(acc, m_t[0], acc, a64::lsl(1));
				break;
			case DspTransform::foldMirror:
				a.and_(acc, acc, 0xffffff);
				a.eor(m_t[0], acc, acc, a64::lsr(1));
				a.tst(m_t[0], 0x400000);
				a.eor(m_t[1], acc, 0x7fffff);
				a.csel(acc, m_t[1], acc, Cond::kNE);
				a.sbfx(acc, acc, 0, 24);
				narrowed = true;
				break;
			case DspTransform::onesComplementNegative:
				a.sbfx(acc, acc, 0, 24);
				a.cmp(acc, 0);
				a.cinv(acc, acc, Cond::kLT);
				narrowed = true;
				break;
			default:
				break;
			}
			if (_sext24)
			{
				a.sbfx(acc, acc, 0, 24);
				narrowed = true;
			}
			if (!narrowed)
				a.sbfx(acc, acc, 0, 29);
			put(_chip.acc, acc);
		}

		void emitConsume(Chip& _chip, const size_t _port)
		{
			namespace a64 = asmjit::a64;
			auto& a = *m_asm;
			const auto skip = a.newLabel();
			a.ldr(m_t[0].w(), st(_chip, offsetof(DspState, serialInputIndex) + _port * 4));
			a.ldr(m_t[1].w(), st(_chip, offsetof(DspState, serialInputCount) + _port * 4));
			a.cmp(m_t[0].w(), m_t[1].w());
			a.b(Cond::kHS, skip);
			a.add(m_t[1], _chip.state, stateOffset(offsetof(DspState, serialInput) + _port * dsp::nSerialWords * 4));
			a.ldrsw(m_t[1], a64::ptr(m_t[1], m_t[0], a64::lsl(2)));
			put(_port == 0 ? _chip.nodeA : _chip.nodeB, m_t[1]);
			a.add(m_t[0], m_t[0], 1);
			a.str(m_t[0].w(), st(_chip, offsetof(DspState, serialInputIndex) + _port * 4));
			a.bind(skip);
		}

		// Queue a read into the first free runtime entry: value in _value, countdown _countdown.
		void emitQueueRead(Chip& _chip, const Gp& _value, const int _countdown)
		{
			namespace a64 = asmjit::a64;
			auto& a = *m_asm;
			const auto done = a.newLabel();
			for (int e = 0; e < 2; ++e)
			{
				const auto next = a.newLabel();
				a.ldr(m_t[0].w(), st(_chip, queueOffset(e, false)));
				a.tbz(m_t[0], 31, next);
				a.str(_value.w(), st(_chip, queueOffset(e, true)));
				a.mov(m_t[0], _countdown);
				a.str(m_t[0].w(), st(_chip, queueOffset(e, false)));
				a.b(done);
				a.bind(next);
			}
			a.bind(done);
		}

		void emitOp(Chip& _chip, const FlatOp& _op, const size_t _pc)
		{
			namespace a64 = asmjit::a64;
			auto& a = *m_asm;
			const auto i = static_cast<size_t>(_chip.index);
			switch (_op.kind)
			{
			case FlatOpKind::eramArm:
				put(_chip.wv, get(_op.a ? _chip.iram : _chip.acc, m_t[0]));
				break;
			case FlatOpKind::eramWrite:
				loadEramOffset(_chip, m_t[0], _op.index);
				eramAddress(_chip, m_t[0], m_t[1], m_t[2]);
				encode24(m_t[2], get(_chip.wv, m_t[2]));
				a.str(m_t[2].w(), a64::ptr(m_t[1], m_t[0], a64::lsl(2)));
				break;
			case FlatOpKind::eramLand:
				loadEramOffset(_chip, m_t[0], _op.index);
				eramAddress(_chip, m_t[0], m_t[1], m_t[2]);
				eramLoad(m_t[2], m_t[1], m_t[0]);
				put(_chip.eram, m_t[2]);
				a.str(m_t[2].w(), st(_chip, queueOffset(_op.a, true)));
				break;
			case FlatOpKind::eramIndexed:
				a.ubfx(m_t[0], get(_chip.acc, m_t[0]), 12, 16);
				a.strh(m_t[0].w(), st(_chip, offsetof(DspState, eramIndexedOffset)));
				eramAddress(_chip, m_t[0], m_t[1], m_t[2]);
				eramLoad(m_t[2], m_t[1], m_t[0]);
				a.str(m_t[2].w(), st(_chip, queueOffset(_op.a, true)));
				break;
			case FlatOpKind::eramLandIndexed:
				a.ldrsw(m_t[0], st(_chip, queueOffset(_op.a, true)));
				put(_chip.eram, m_t[0]);
				break;
			case FlatOpKind::eramGeneric:
				{
					const auto notPending = a.newLabel();
					const auto end = a.newLabel();
					a.ldrb(m_t[0].w(), st(_chip, offsetof(DspState, eramPrefixPending)));
					a.cbz(m_t[0], notPending);
					// Second word: consume the pending command.
					a.ldrb(m_t[0].w(), st(_chip, offsetof(DspState, eramOffsetHigh)));
					a.ldrh(m_t[1].w(), a64::ptr(_chip.params, stateOffset(offsetof(DspParams, eramOffsetLow) + _op.index * 2)));
					a.orr(m_t[0], m_t[1], m_t[0], a64::lsl(9));
					eramAddress(_chip, m_t[0], m_t[1], m_t[2]);
					{
						const auto read = a.newLabel();
						const auto consumed = a.newLabel();
						a.ldrb(m_t[2].w(), st(_chip, offsetof(DspState, eramPendingWrite)));
						a.cbz(m_t[2], read);
						encode24(m_t[2], get(_chip.wv, m_t[2]));
						a.str(m_t[2].w(), a64::ptr(m_t[1], m_t[0], a64::lsl(2)));
						a.b(consumed);
						a.bind(read);
						eramLoad(m_t[2], m_t[1], m_t[0]);
						emitQueueRead(_chip, m_t[2], 1);
						a.bind(consumed);
					}
					a.strb(a64::wzr, st(_chip, offsetof(DspState, eramPrefixPending)));
					a.mov(m_t[0], 1);
					a.strb(m_t[0].w(), fr(offsetof(DspJitFrame, portBusy) + i));
					a.b(end);
					a.bind(notPending);
					a.strb(a64::wzr, fr(offsetof(DspJitFrame, portBusy) + i));
					if (_op.a != static_cast<uint8_t>(DspEramArm::none))
					{
						a.mov(m_t[0], 1);
						a.strb(m_t[0].w(), st(_chip, offsetof(DspState, eramPrefixPending)));
						a.mov(m_t[0], _op.a != static_cast<uint8_t>(DspEramArm::read) ? 1 : 0);
						a.strb(m_t[0].w(), st(_chip, offsetof(DspState, eramPendingWrite)));
						a.ldrb(m_t[0].w(), a64::ptr(_chip.params, stateOffset(offsetof(DspParams, eramOffsetHigh) + _op.index)));
						a.strb(m_t[0].w(), st(_chip, offsetof(DspState, eramOffsetHigh)));
						put(_chip.wv, get(_op.a == static_cast<uint8_t>(DspEramArm::writeIramLatch) ? _chip.iram : _chip.acc, m_t[0]));
					}
					a.bind(end);
					break;
				}
			case FlatOpKind::eramAdvance:
				for (int e = 0; e < 2; ++e)
				{
					const auto next = a.newLabel();
					a.ldr(m_t[0].w(), st(_chip, queueOffset(e, false)));
					a.tbnz(m_t[0], 31, next);
					a.subs(m_t[0].w(), m_t[0].w(), 1);
					a.str(m_t[0].w(), st(_chip, queueOffset(e, false)));
					a.b(Cond::kNE, next);
					a.ldrsw(m_t[1], st(_chip, queueOffset(e, true)));
					put(_chip.eram, m_t[1]);
					a.mov(m_t[0], -1);
					a.str(m_t[0].w(), st(_chip, queueOffset(e, false)));
					a.bind(next);
				}
				break;
			case FlatOpKind::eramIndexedGeneric:
				{
					const auto skip = a.newLabel();
					a.ldrb(m_t[0].w(), fr(offsetof(DspJitFrame, portBusy) + i));
					a.cbnz(m_t[0], skip);
					a.ubfx(m_t[0], get(_chip.acc, m_t[0]), 12, 16);
					a.strh(m_t[0].w(), st(_chip, offsetof(DspState, eramIndexedOffset)));
					eramAddress(_chip, m_t[0], m_t[1], m_t[2]);
					eramLoad(m_t[2], m_t[1], m_t[0]);
					emitQueueRead(_chip, m_t[2], 2);
					a.bind(skip);
					break;
				}
			case FlatOpKind::iramWriteAcc:
				encode24(m_t[0], get(_chip.acc, m_t[0]));
				a.str(m_t[0].w(), iramCell(_chip, _op.a, _op.b));
				break;
			case FlatOpKind::iramWriteLatch:
				a.and_(m_t[0], get(_chip.eram, m_t[0]), 0xffffff);
				a.str(m_t[0].w(), iramCell(_chip, _op.a, _op.b));
				break;
			case FlatOpKind::iramRead:
				a.ldr(m_t[0].w(), iramCell(_chip, _op.a, _op.b));
				a.sbfx(m_t[0], m_t[0], 0, 24);
				put(_chip.iram, m_t[0]);
				break;
			case FlatOpKind::iramReadParam:
				a.ldr(m_t[0].w(), st(_chip, offsetof(DspState, iram3) + _op.b * 4));
				a.ubfx(m_t[0], m_t[0], 10, 16);
				a.strh(m_t[0].w(), st(_chip, offsetof(DspState, iram3ParameterLatch)));
				break;
			case FlatOpKind::emitA:
				a.ldr(m_t[0].w(), a64::ptr(_chip.procBank, _op.b * 4));
				a.sbfx(m_t[0], m_t[0], 0, 24);
				a.str(m_t[0].w(), st(_chip, offsetof(DspState, serialOutput) + _op.a * 4));
				break;
			case FlatOpKind::emitBcd:
				a.ldr(m_t[0].w(), a64::ptr(_chip.procBank, _op.b * 4));
				a.sbfx(m_t[0], m_t[0], 0, 24);
				if (_op.c != 0xff)
					a.str(m_t[0].w(), st(_chip, offsetof(DspState, serialOutput) + (1 + _op.a) * dsp::nSerialWords * 4 + _op.c * 4));
				break;
			case FlatOpKind::emitADynamic:
				{
					a.ldr(m_t[0].w(), st(_chip, offsetof(DspState, outputWordPosition)));
					a.and_(m_t[1], m_t[0], dsp::nIramSlots - 1);
					a.ldr(m_t[1].w(), a64::ptr(_chip.procBank, m_t[1], a64::lsl(2)));
					a.sbfx(m_t[1], m_t[1], 0, 24);
					a.add(m_t[0], m_t[0], 1);
					a.str(m_t[0].w(), st(_chip, offsetof(DspState, outputWordPosition)));
					if (_op.a)
					{
						const auto skip = a.newLabel();
						a.ldr(m_t[2].w(), st(_chip, offsetof(DspState, serialOutputCount)));
						a.cmp(m_t[2], dsp::nSerialWords);
						a.b(Cond::kHS, skip);
						a.add(m_t[3], _chip.state, stateOffset(offsetof(DspState, serialOutput)));
						a.str(m_t[1].w(), a64::ptr(m_t[3], m_t[2], a64::lsl(2)));
						a.add(m_t[2], m_t[2], 1);
						a.str(m_t[2].w(), st(_chip, offsetof(DspState, serialOutputCount)));
						if (_chip.peerListens)
						{
							a.str(m_t[1].w(), fr(offsetof(DspJitFrame, linkWord) + i * sizeof(int32_t)));
							a.mov(m_t[2], 1);
							a.strb(m_t[2].w(), fr(offsetof(DspJitFrame, linkFlag) + i));
						}
						a.bind(skip);
					}
					break;
				}
			case FlatOpKind::emitBcdDynamic:
				{
					const Gp port = m_t[2];
					const Gp value = m_t[1];
					a.ldrb(port.w(), st(_chip, offsetof(DspState, dacPortPosition)));
					a.ldr(m_t[0].w(), st(_chip, offsetof(DspState, outputWordPosition)));
					a.and_(value, m_t[0], dsp::nIramSlots - 1);
					a.ldr(value.w(), a64::ptr(_chip.procBank, value, a64::lsl(2)));
					a.sbfx(value, value, 0, 24);
					a.add(m_t[0], m_t[0], 1);
					a.str(m_t[0].w(), st(_chip, offsetof(DspState, outputWordPosition)));
					if (_op.a != 0)
					{
						const auto skipStore = a.newLabel();
						const auto noBus = skipStore;
						a.mov(m_t[0], _op.a);
						a.lsr(m_t[0], m_t[0], port);
						a.tbz(m_t[0], 0, skipStore);
						// serialOutputCount[1 + port]
						a.add(m_t[3], _chip.state, stateOffset(offsetof(DspState, serialOutputCount) + 4));
						a.ldr(m_t[0].w(), a64::ptr(m_t[3], port, a64::lsl(2)));
						a.cmp(m_t[0], dsp::nSerialWords);
						a.b(Cond::kHS, noBus);
						a.add(m_t[3], _chip.state, stateOffset(offsetof(DspState, serialOutput) + dsp::nSerialWords * 4));
						a.add(m_t[3], m_t[3], port, a64::lsl(7));
						a.str(value.w(), a64::ptr(m_t[3], m_t[0], a64::lsl(2)));
						a.add(m_t[0], m_t[0], 1);
						a.add(m_t[3], _chip.state, stateOffset(offsetof(DspState, serialOutputCount) + 4));
						a.str(m_t[0].w(), a64::ptr(m_t[3], port, a64::lsl(2)));
						a.bind(skipStore);
					}
					a.add(port, port, 1);
					a.cmp(port, 3);
					a.csel(port, a64::xzr, port, Cond::kEQ);
					a.strb(port.w(), st(_chip, offsetof(DspState, dacPortPosition)));
					break;
				}
			case FlatOpKind::pins:
				a.ldrb(m_t[0].w(), st(_chip, offsetof(DspState, outputPins)));
				if ((_op.a & 1) == 0)
				{
					a.tst(m_t[0], 1);
					a.orr(m_t[1], m_t[0], 4);
					a.csel(m_t[0], m_t[1], m_t[0], Cond::kNE);
				}
				a.and_(m_t[0], m_t[0], 4);
				if (_op.a != 0)
					a.orr(m_t[0], m_t[0], _op.a);
				a.strb(m_t[0].w(), st(_chip, offsetof(DspState, outputPins)));
				break;
			case FlatOpKind::consumeA:
				emitConsume(_chip, 0);
				break;
			case FlatOpKind::consumeB:
				emitConsume(_chip, 1);
				break;
			case FlatOpKind::consumeBDynamic:
				{
					const auto skip = a.newLabel();
					a.ldrb(m_t[0].w(), st(_chip, offsetof(DspState, dacPortPosition)));
					a.cmp(m_t[0], 1);
					a.b(Cond::kNE, skip);
					emitConsume(_chip, 1);
					a.bind(skip);
					break;
				}
			case FlatOpKind::mulCapture:
				multiplySource(_chip, m_cap[0], static_cast<DspMultiplyInput>(_op.a));
				if (_op.b == _op.a)
					a.mov(m_cap[1], m_cap[0]);
				else
					multiplySource(_chip, m_cap[1], static_cast<DspMultiplyInput>(_op.b));
				break;
			case FlatOpKind::factorCapture:
				{
					const auto kind = static_cast<DspMultiplyFactor>(_op.a);
					if (kind != DspMultiplyFactor::iram3Parameter)
						sat24(m_t[0], get(_chip.acc, m_t[0]));
					switch (kind)
					{
					case DspMultiplyFactor::accLow12Shl3:
						a.ubfx(m_cap[2], m_t[0], 0, 12);
						a.lsl(m_cap[2], m_cap[2], 3);
						break;
					case DspMultiplyFactor::accBits22to8:
						a.ubfx(m_cap[2], m_t[0], 8, 15);
						break;
					case DspMultiplyFactor::accShr8:
						a.asr(m_cap[2], m_t[0], 8);
						break;
					default:
						a.ldrsh(m_cap[2], st(_chip, offsetof(DspState, iram3ParameterLatch)));
						break;
					}
					if (_op.b)
					{
						a.mov(m_t[0], 0x7fff);
						a.sub(m_cap[2], m_t[0], m_cap[2]);
						if (kind >= DspMultiplyFactor::accShr8)
						{
							a.mov(m_t[0], 0x8000);
							a.sub(m_cap[2], m_cap[2], m_t[0]);
						}
					}
					break;
				}
			case FlatOpKind::alu:
				{
					const auto kind = static_cast<DspAlu>(_op.a);
					if (kind >= DspAlu::andImm && kind <= DspAlu::negAccPlusImm)
						loadParam(_chip, m_t[0], _op.index);
					emitAlu(_chip, kind, m_t[0]);
					break;
				}
			case FlatOpKind::parallel:
				emitAlu(_chip, static_cast<DspAlu>(_op.a), m_t[0], false);
				emitTransform(_chip, static_cast<DspTransform>(_op.b), _op.c != 0);
				break;
			case FlatOpKind::mulCoefficient:
				loadParam(_chip, m_t[0], _op.index);
				a.mul(m_t[0], m_cap[0], m_t[0]);
				emitNegativeFraction(_chip, m_t[0], 13);
				divideTruncating(m_t[0], m_t[0], 13, m_t[1]);
				sat29(m_t[0], m_t[0]);
				put(_chip.mul, m_t[0]);
				put(_chip.fb, m_cap[1]);
				break;
			case FlatOpKind::mulFactor:
				a.mul(m_t[0], m_cap[0], m_cap[2]);
				if (_op.a != 0)
					a.lsl(m_t[0], m_t[0], _op.a);
				emitNegativeFraction(_chip, m_t[0], 15);
				divideTruncating(m_t[0], m_t[0], 15, m_t[1]);
				sat29(m_t[0], m_t[0]);
				put(_chip.mul, m_t[0]);
				put(_chip.fb, m_cap[1]);
				break;
			case FlatOpKind::branch:
				{
					if (m_jumpsMode)
					{
						// Emitted after the slot's remaining ops and the frame-end check by emitJumpsProgram.
						m_pendingBranch = true;
						m_pendingCondition = static_cast<DspBranch>(_op.a);
						m_pendingTarget = _op.index;
						break;
					}
					// The next program counter of this body: the fall-through slot, or the target when the
					// condition holds on the pre-slot accumulator (a branch slot has no datapath op).
					a.mov(m_t[0], _pc + 1);
					const auto branch = static_cast<DspBranch>(_op.a);
					if (branch == DspBranch::always)
						a.mov(m_t[0], _op.index);
					else
					{
						Cond cond = Cond::kEQ;
						switch (branch)
						{
						case DspBranch::eq0: cond = Cond::kEQ; break;
						case DspBranch::ne0: cond = Cond::kNE; break;
						case DspBranch::ge0: cond = Cond::kGE; break;
						case DspBranch::lt0: cond = Cond::kLT; break;
						case DspBranch::gt0: cond = Cond::kGT; break;
						default: cond = Cond::kLE; break;
						}
						a.mov(m_t[1], _op.index);
						a.cmp(get(_chip.acc, m_t[2]), 0);
						a.csel(m_t[0], m_t[1], m_t[0], cond);
					}
					a.str(m_t[0].w(), fr(offsetof(DspJitFrame, pc) + i * sizeof(uint32_t)));
					m_pcNextWritten = true;
					break;
				}
			}
		}

		asmjit::JitRuntime m_runtime;
		DspJitRun m_run = nullptr;
		asmjit::Error m_lastError = asmjit::kErrorOk;

		// Per-compile state.
		asmjit::a64::Builder* m_asm = nullptr;
		std::vector<Chip> m_chips;
		std::vector<Gp> m_pool;
		size_t m_poolNext = 0;
		Gp m_t[4];    // general temporaries
		Gp m_cap[3];  // multiply captures: input, feedback source, factor
		Gp m_c24, m_c29;
		bool m_pcNextWritten = false;
		bool m_jumpsMode = false;
		bool m_pendingBranch = false;
		DspBranch m_pendingCondition = DspBranch::never;
		uint16_t m_pendingTarget = 0;
		asmjit::Label m_jumpsExit;
	};
} // namespace xpLib
