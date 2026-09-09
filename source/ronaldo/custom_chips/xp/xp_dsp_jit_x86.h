#pragma once

// x86-64 backend of the XP DSP JIT, the mirror of xp_dsp_jit_arm64.h: the same frame function shape (static
// slots in cycle order with the deposits ahead of them, a linked partner interleaved cycle by cycle, bodies
// and a jump table for a chip with a reachable branch) on fifteen general registers. State, parameters and
// the IRAM banks of the first chip stay in registers; what does not fit is reached through memory operands.
// The multiply captures live in xmm0-2. Constants are immediates.

#include <asmjit/asmjit.h>
#include <asmjit/x86.h>

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

		bool compile(const FlatProgram& _a, const FlatProgram* _b)
		{
			namespace x86 = asmjit::x86;
			release();

			asmjit::CodeHolder code;
			code.init(m_runtime.environment());
			x86::Builder builder(&code);
			m_asm = &builder;
			m_chips.clear();
			m_chips.push_back(Chip{&_a, 0});
			if (_b != nullptr)
				m_chips.push_back(Chip{_b, 1});
			if (!allocateRegisters())
				return false;

			auto& a = *m_asm;
			for (const auto& r : {x86::rbx, x86::rbp, x86::rsi, x86::rdi, x86::r12, x86::r13, x86::r14, x86::r15})
				a.push(r);
#if defined(_WIN32)
			a.mov(frameReg(), x86::rcx);
#else
			a.mov(frameReg(), x86::rdi);
#endif
			for (auto& chip : m_chips)
				emitChipEntry(chip);

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

			for (const auto& r : {x86::r15, x86::r14, x86::r13, x86::r12, x86::rdi, x86::rsi, x86::rbp, x86::rbx})
				a.pop(r);
			a.ret();

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
		using Gp = asmjit::x86::Gpq;
		using Mem = asmjit::x86::Mem;

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
			Gp state, params;
			Gp skip; // jumps form: slots skipped so far this frame
			Loc mixBank, procBank;
			Loc acc, iram, mul, fb, eram, nodeA;
			Loc wv, mask, eramPos, mixer, nodeB;
			std::vector<asmjit::Label> bodies;
			asmjit::Label idle, table;
		};

		static Gp frameReg() { return asmjit::x86::r15; }
		static int32_t off(const size_t _offset) { return static_cast<int32_t>(_offset); }

		Mem st8(const Chip& _chip, const size_t _offset) const { return asmjit::x86::qword_ptr(_chip.state, off(_offset)); }
		Mem st4(const Chip& _chip, const size_t _offset) const { return asmjit::x86::dword_ptr(_chip.state, off(_offset)); }
		Mem st2(const Chip& _chip, const size_t _offset) const { return asmjit::x86::word_ptr(_chip.state, off(_offset)); }
		Mem st1(const Chip& _chip, const size_t _offset) const { return asmjit::x86::byte_ptr(_chip.state, off(_offset)); }
		Mem fr8(const size_t _offset) const { return asmjit::x86::qword_ptr(frameReg(), off(_offset)); }
		Mem fr4(const size_t _offset) const { return asmjit::x86::dword_ptr(frameReg(), off(_offset)); }
		Mem fr1(const size_t _offset) const { return asmjit::x86::byte_ptr(frameReg(), off(_offset)); }

		// ---- register allocation ----
		bool allocateRegisters()
		{
			namespace x86 = asmjit::x86;
			m_pool = {x86::rbx, x86::rbp, x86::r12, x86::r13, x86::r14, x86::rsi, x86::rdi,
					  x86::r8,  x86::r9,  x86::r10, x86::rax, x86::rcx, x86::rdx, x86::r11};
			m_poolNext = 0;
			const auto take = [this](Gp& _reg)
			{
				if (m_poolNext >= m_pool.size())
					return false;
				_reg = m_pool[m_poolNext++];
				return true;
			};
			const auto value = [&](Loc& _loc, const Gp& _base, const size_t _offset, const uint8_t _size,
								   const bool _signed)
			{
				_loc.inReg = take(_loc.reg);
				_loc.base = _base;
				_loc.offset = off(_offset);
				_loc.size = _size;
				_loc.isSigned = _signed;
			};

			for (auto& chip : m_chips)
			{
				chip.dynamic = chip.program->dynamic;
				chip.slots = chip.program->config.executionSlots;
				if (!take(chip.state) || !take(chip.params))
					return false;
			}
			for (auto& t : m_t)
				if (!take(t))
					return false;
			for (auto& chip : m_chips)
				if (chip.program->jumps && !take(chip.skip))
					return false;
			for (auto& chip : m_chips)
			{
				const auto i = static_cast<size_t>(chip.index);
				value(chip.mixBank, frameReg(), offsetof(DspJitFrame, mixerBank) + i * sizeof(void*), 8, false);
				value(chip.procBank, frameReg(), offsetof(DspJitFrame, processingBank) + i * sizeof(void*), 8, false);
				value(chip.acc, chip.state, offsetof(DspState, accumulator), 8, true);
				value(chip.iram, chip.state, offsetof(DspState, iramReadLatch), 8, true);
				value(chip.mul, chip.state, offsetof(DspState, multiplyResultLatch), 8, true);
				value(chip.fb, chip.state, offsetof(DspState, multiplyFeedbackLatch), 8, true);
				value(chip.eram, chip.state, offsetof(DspState, eramReadLatch), 8, true);
				value(chip.nodeA, chip.state, offsetof(DspState, serialInputNode), 8, true);
			}
			for (auto& chip : m_chips)
			{
				const auto i = static_cast<size_t>(chip.index);
				value(chip.eramPos, chip.state, offsetof(DspState, eramPos), 4, false);
				value(chip.mask, chip.state, offsetof(DspState, mixerInitialized), 8, false);
				value(chip.mixer, frameReg(), offsetof(DspJitFrame, mixer) + i * sizeof(void*), 8, false);
				value(chip.wv, chip.state, offsetof(DspState, eramPendingWriteValue), 8, true);
				value(chip.nodeB, chip.state, offsetof(DspState, serialInputNode) + sizeof(int64_t), 8, true);
			}
			if (m_chips.size() == 2)
			{
				m_chips[0].peerListens = m_chips[1].program->config.serialInputEnabled != 0;
				m_chips[1].peerListens = m_chips[0].program->config.serialInputEnabled != 0;
			}
			return true;
		}

		// ---- value access ----
		Mem locMem(const Loc& _loc) const
		{
			namespace x86 = asmjit::x86;
			switch (_loc.size)
			{
			case 8:
				return x86::qword_ptr(_loc.base, _loc.offset);
			case 4:
				return x86::dword_ptr(_loc.base, _loc.offset);
			case 2:
				return x86::word_ptr(_loc.base, _loc.offset);
			default:
				return x86::byte_ptr(_loc.base, _loc.offset);
			}
		}

		Gp get(const Loc& _loc, const Gp& _tmp)
		{
			if (_loc.inReg)
				return _loc.reg;
			const auto mem = locMem(_loc);
			switch (_loc.size)
			{
			case 8:
				m_asm->mov(_tmp, mem);
				break;
			case 4:
				if (_loc.isSigned)
					m_asm->movsxd(_tmp, mem);
				else
					m_asm->mov(_tmp.r32(), mem);
				break;
			case 2:
				if (_loc.isSigned)
					m_asm->movsx(_tmp, mem);
				else
					m_asm->movzx(_tmp.r32(), mem);
				break;
			default:
				m_asm->movzx(_tmp.r32(), mem);
				break;
			}
			return _tmp;
		}

		void put(const Loc& _loc, const Gp& _src)
		{
			if (_loc.inReg)
			{
				if (_loc.reg != _src)
					m_asm->mov(_loc.reg, _src);
				return;
			}
			const auto mem = locMem(_loc);
			switch (_loc.size)
			{
			case 8:
				m_asm->mov(mem, _src);
				break;
			case 4:
				m_asm->mov(mem, _src.r32());
				break;
			case 2:
				m_asm->mov(mem, _src.r16());
				break;
			default:
				m_asm->mov(mem, _src.r8());
				break;
			}
		}

		void capSet(const int _index, const Gp& _src) { m_asm->movq(asmjit::x86::Xmm(_index), _src); }
		void capGet(const int _index, const Gp& _dst) { m_asm->movq(_dst, asmjit::x86::Xmm(_index)); }

		// ---- arithmetic helpers ----
		void sat(const Gp& _dst, const Gp& _src, const int64_t _min, const int64_t _max, const Gp& _tmp)
		{
			if (_dst != _src)
				m_asm->mov(_dst, _src);
			m_asm->mov(_tmp, _min);
			m_asm->cmp(_dst, _tmp);
			m_asm->cmovl(_dst, _tmp);
			m_asm->mov(_tmp, _max);
			m_asm->cmp(_dst, _tmp);
			m_asm->cmovg(_dst, _tmp);
		}
		void sat24(const Gp& _dst, const Gp& _src, const Gp& _tmp) { sat(_dst, _src, -0x800000, 0x7fffff, _tmp); }
		void sat29(const Gp& _dst, const Gp& _src, const Gp& _tmp) { sat(_dst, _src, -0x10000000, 0x0fffffff, _tmp); }

		void sext(const Gp& _reg, const unsigned _bits)
		{
			m_asm->shl(_reg, 64 - _bits);
			m_asm->sar(_reg, 64 - _bits);
		}

		// dst = dst / 2^shift, truncating toward zero.
		void divideTruncating(const Gp& _dst, const unsigned _shift, const Gp& _tmp)
		{
			m_asm->mov(_tmp, _dst);
			m_asm->sar(_tmp, 63);
			m_asm->shr(_tmp, 64 - _shift);
			m_asm->add(_dst, _tmp);
			m_asm->sar(_dst, _shift);
		}

		void encode24(const Gp& _dst, const Gp& _src, const Gp& _tmp)
		{
			sat24(_dst, _src, _tmp);
			m_asm->and_(_dst, 0xffffff);
		}

		// Stores whether a multiply numerator is negative with a nonzero remainder below 2^_shift; t2 and t3
		// are clobbered.
		void emitNegativeFraction(Chip& _chip, const Gp& _numerator, const unsigned _shift)
		{
			auto& a = *m_asm;
			a.mov(m_t[2], _numerator);
			a.sar(m_t[2], 63);
			a.xor_(m_t[3].r32(), m_t[3].r32());
			a.test(_numerator, static_cast<int32_t>((int64_t{1} << _shift) - 1));
			a.setnz(m_t[3].r8());
			a.and_(m_t[2], m_t[3]);
			a.mov(st1(_chip, offsetof(DspState, multiplyNegativeFraction)), m_t[2].r8());
		}

		// The IRAM cell of a bank; a bank held in memory is first loaded into _tmp.
		Mem iramCell(const Chip& _chip, const uint8_t _bank, const uint8_t _index, const Gp& _tmp)
		{
			namespace x86 = asmjit::x86;
			const auto byteOffset = static_cast<int32_t>(_index) * 4;
			switch (static_cast<DspIramBank>(_bank))
			{
			case DspIramBank::mixer:
				return x86::dword_ptr(get(_chip.mixBank, _tmp), byteOffset);
			case DspIramBank::processing:
				return x86::dword_ptr(get(_chip.procBank, _tmp), byteOffset);
			case DspIramBank::direct1:
				return st4(_chip, offsetof(DspState, iram1) + byteOffset);
			case DspIramBank::direct2:
				return st4(_chip, offsetof(DspState, iram2) + byteOffset);
			default:
				return st4(_chip, offsetof(DspState, iram3) + byteOffset);
			}
		}

		// eram[(eramPos + offset) & 0xffff] as a memory operand; the index register is finished in _offset.
		Mem eramCell(const Chip& _chip, const Gp& _offset, const Gp& _tmp)
		{
			m_asm->add(_offset.r32(), get(_chip.eramPos, _tmp).r32());
			m_asm->and_(_offset.r32(), 0xffff);
			return asmjit::x86::dword_ptr(_chip.state, _offset, 2, off(offsetof(DspState, eram)));
		}

		void loadEram(const Gp& _dst, const Mem& _cell)
		{
			m_asm->mov(_dst.r32(), _cell);
			sext(_dst, 24);
		}

		void loadParam(const Chip& _chip, const Gp& _dst, const uint16_t _slot)
		{
			m_asm->movsxd(_dst, asmjit::x86::dword_ptr(_chip.params, off(offsetof(DspParams, param) + _slot * 4)));
		}

		void loadEramOffset(const Chip& _chip, const Gp& _dst, const uint16_t _slot)
		{
			m_asm->movzx(_dst.r32(), asmjit::x86::word_ptr(_chip.params, off(offsetof(DspParams, eramOffset) + _slot * 2)));
		}

		static size_t queueOffset(const int _entry, const bool _value)
		{
			return offsetof(DspState, pendingEramReads) + _entry * sizeof(DspState::PendingEramRead) +
				(_value ? offsetof(DspState::PendingEramRead, value) : offsetof(DspState::PendingEramRead, countdown));
		}

		// ---- prologue / epilogue ----
		void emitChipEntry(Chip& _chip)
		{
			auto& a = *m_asm;
			const auto i = static_cast<size_t>(_chip.index);
			a.mov(_chip.state, fr8(offsetof(DspJitFrame, state) + i * sizeof(void*)));
			a.mov(_chip.params, fr8(offsetof(DspJitFrame, params) + i * sizeof(void*)));
			for (const Loc* loc : {&_chip.mixBank, &_chip.procBank, &_chip.acc, &_chip.iram, &_chip.mul, &_chip.fb, &_chip.eram,
								   &_chip.nodeA, &_chip.eramPos, &_chip.mask, &_chip.mixer, &_chip.wv, &_chip.nodeB})
				if (loc->inReg)
					get(Loc{Gp{}, false, loc->base, loc->offset, loc->size, loc->isSigned}, loc->reg);
			if (_chip.dynamic)
				a.mov(fr4(offsetof(DspJitFrame, pc) + i * sizeof(uint32_t)), 0);
			a.mov(fr1(offsetof(DspJitFrame, linkFlag) + i), 0);
		}

		void emitChipExit(Chip& _chip)
		{
			auto& a = *m_asm;
			for (const Loc* loc : {&_chip.acc, &_chip.iram, &_chip.mul, &_chip.fb, &_chip.eram, &_chip.nodeA, &_chip.mask,
								   &_chip.wv, &_chip.nodeB})
				if (loc->inReg)
					put(Loc{Gp{}, false, loc->base, loc->offset, loc->size, loc->isSigned}, loc->reg);
			if (_chip.dynamic)
				return;
			const auto& carry = _chip.program->carry;
			a.mov(st4(_chip, offsetof(DspState, outputWordPosition)), carry.outputWordPosition);
			a.mov(st1(_chip, offsetof(DspState, dacPortPosition)), carry.dacPortPosition);
			for (size_t bus = 0; bus < dsp::nSerialBuses; ++bus)
				a.mov(st4(_chip, offsetof(DspState, serialOutputCount) + bus * 4), carry.serialOutputCount[bus]);
			if (!carry.staticRegion)
				return;
			a.mov(st1(_chip, offsetof(DspState, eramPrefixPending)), carry.prefixPendingAtEnd);
			if (carry.hasArm)
			{
				a.mov(st1(_chip, offsetof(DspState, eramPendingWrite)), carry.lastArmIsWrite);
				a.movzx(m_t[0].r32(), asmjit::x86::byte_ptr(_chip.params, off(offsetof(DspParams, eramOffsetHigh) + carry.lastArmSlot)));
				a.mov(st1(_chip, offsetof(DspState, eramOffsetHigh)), m_t[0].r8());
			}
			for (int e = 0; e < 2; ++e)
			{
				const auto& entry = carry.queue[e];
				a.mov(st4(_chip, queueOffset(e, false)), static_cast<int32_t>(entry.countdown));
				if (entry.countdown < 0 || entry.indexed)
					continue;
				loadEramOffset(_chip, m_t[0], entry.slot);
				loadEram(m_t[1], eramCell(_chip, m_t[0], m_t[2]));
				a.mov(st4(_chip, queueOffset(e, true)), m_t[1].r32());
			}
		}

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

		// The jumps form, see the arm64 backend.
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
			a.xor_(_chip.skip.r32(), _chip.skip.r32());
			for (size_t pc = 0; pc < flat.slotsLowered; ++pc)
			{
				if (targets[pc].isValid())
					a.bind(targets[pc]);
				m_pendingBranch = false;
				emitSlot(_chip, pc);
				if (pc + 1 >= budget)
				{
					a.cmp(_chip.skip, static_cast<int32_t>(pc + 1 - budget));
					a.je(m_jumpsExit);
				}
				if (!m_pendingBranch || m_pendingTarget >= flat.slotsLowered || m_pendingTarget <= pc + 1)
					continue;
				const auto skipped = static_cast<int32_t>(m_pendingTarget - pc - 1);
				if (m_pendingCondition == DspBranch::always)
				{
					a.add(_chip.skip, skipped);
					a.jmp(targets[m_pendingTarget]);
					continue;
				}
				const auto fallthrough = a.newLabel();
				a.cmp(get(_chip.acc, m_t[0]), 0);
				switch (m_pendingCondition)
				{
				case DspBranch::eq0: a.jne(fallthrough); break;
				case DspBranch::ne0: a.je(fallthrough); break;
				case DspBranch::ge0: a.jl(fallthrough); break;
				case DspBranch::lt0: a.jge(fallthrough); break;
				case DspBranch::gt0: a.jle(fallthrough); break;
				default: a.jg(fallthrough); break;
				}
				a.add(_chip.skip, skipped);
				a.jmp(targets[m_pendingTarget]);
				a.bind(fallthrough);
			}
			a.bind(m_jumpsExit);
			m_jumpsMode = false;
		}

		// ---- mixer deposits ----
		void emitDeposits(Chip& _chip, const size_t _cycle)
		{
			namespace x86 = asmjit::x86;
			auto& a = *m_asm;
			const auto phase = _cycle & 3;
			if (phase == 1)
				return;
			const auto voice = _cycle >> 2;
			const Gp dest = m_t[0];
			const Gp contribution = m_t[1];
			const Gp value = m_t[2];
			// A null mixer frame means the deposits were hoisted ahead of the frame, or there are none.
			const auto none = a.newLabel();
			{
				const Gp mixer = get(_chip.mixer, m_t[3]);
				a.test(mixer, mixer);
				a.jz(none);
			}
			const auto deposit = [&](const size_t _send)
			{
				const auto offset = off((voice * 4 + _send) * sizeof(DspMixerSend));
				const auto skip = a.newLabel();
				const auto load = a.newLabel();
				const auto add = a.newLabel();
				const Gp mixer = get(_chip.mixer, m_t[3]);
				a.mov(dest, x86::qword_ptr(mixer, offset + off(offsetof(DspMixerSend, destination))));
				a.cmp(dest, dsp::nIramSlots);
				a.jae(skip);
				a.mov(contribution, x86::qword_ptr(mixer, offset + off(offsetof(DspMixerSend, contribution))));
				const Gp bank = get(_chip.mixBank, m_t[3]);
				if (_chip.mask.inReg)
				{
					a.bt(_chip.mask.reg, dest);
					a.jc(load);
					a.bts(_chip.mask.reg, dest);
				}
				else
				{
					a.bt(locMem(_chip.mask), dest);
					a.jc(load);
					a.bts(locMem(_chip.mask), dest);
				}
				a.xor_(value.r32(), value.r32());
				a.jmp(add);
				a.bind(load);
				a.mov(value.r32(), x86::dword_ptr(bank, dest, 2));
				sext(value, 24);
				a.bind(add);
				a.add(value, contribution);
				encode24(value, value, contribution);
				a.mov(x86::dword_ptr(bank, dest, 2), value.r32());
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
			a.bind(none);
		}

		// ---- dynamic chips: dispatch and bodies ----
		void emitDispatch(Chip& _chip, const size_t)
		{
			namespace x86 = asmjit::x86;
			auto& a = *m_asm;
			const auto i = static_cast<size_t>(_chip.index);
			const auto continuation = a.newLabel();
			a.mov(m_t[0].r32(), fr4(offsetof(DspJitFrame, pc) + i * sizeof(uint32_t)));
			a.mov(m_t[1].r32(), dsp::nProgramSlots);
			a.cmp(m_t[0].r32(), m_t[1].r32());
			a.cmovae(m_t[0].r32(), m_t[1].r32());
			a.lea(m_t[1], x86::ptr(_chip.table));
			a.lea(m_t[0], x86::ptr(m_t[0], m_t[0], 2)); // * 5: the length of a long jmp
			a.add(m_t[1], m_t[0]);
			a.lea(m_t[2], x86::ptr(continuation));
			a.mov(fr8(offsetof(DspJitFrame, returnAddress)), m_t[2]);
			a.jmp(m_t[1]);
			a.bind(continuation);
		}

		void emitBodies(Chip& _chip)
		{
			auto& a = *m_asm;
			a.bind(_chip.table);
			for (size_t pc = 0; pc < dsp::nProgramSlots; ++pc)
				a.long_().jmp(_chip.bodies[pc]);
			a.long_().jmp(_chip.idle);
			for (size_t pc = 0; pc < dsp::nProgramSlots; ++pc)
			{
				a.bind(_chip.bodies[pc]);
				m_pcNextWritten = false;
				emitSlot(_chip, pc);
				if (!m_pcNextWritten)
					a.mov(fr4(offsetof(DspJitFrame, pc) + _chip.index * sizeof(uint32_t)), static_cast<uint32_t>(pc + 1));
				a.jmp(fr8(offsetof(DspJitFrame, returnAddress)));
			}
			a.bind(_chip.idle);
			a.jmp(fr8(offsetof(DspJitFrame, returnAddress)));
		}

		// ---- the lockstep word handoff ----
		void emitExchange(const size_t _cycle)
		{
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
						a.movsxd(m_t[0], st4(source, offsetof(DspState, serialOutput) + op.a * 4));
						put(peer.nodeA, m_t[0]);
					}
					continue;
				}
				const auto skip = a.newLabel();
				a.cmp(fr1(offsetof(DspJitFrame, linkFlag) + from), 0);
				a.je(skip);
				a.movsxd(m_t[0], fr4(offsetof(DspJitFrame, linkWord) + from * sizeof(int32_t)));
				put(peer.nodeA, m_t[0]);
				a.mov(fr1(offsetof(DspJitFrame, linkFlag) + from), 0);
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
				sat24(_dst, get(_chip.acc, _dst), m_t[3]);
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

		// acc = f(acc, iram, mul, param). The param (or a scratch) is in _param = t0. A primary op wraps the
		// result to 29 bits here; a parallel op keeps the raw result for its transform, which wraps afterwards.
		void emitAlu(Chip& _chip, const DspAlu _alu, const Gp& _param, const bool _wrap = true)
		{
			auto& a = *m_asm;
			const Gp acc = get(_chip.acc, m_t[3]);
			const Gp iram = get(_chip.iram, m_t[1]);
			const Gp mul = get(_chip.mul, m_t[2]);
			switch (_alu)
			{
			case DspAlu::hold:
				return;
			case DspAlu::accPlusIram:
				a.add(acc, iram);
				break;
			case DspAlu::accPlusMul:
				a.add(acc, mul);
				break;
			case DspAlu::iram:
				a.mov(acc, iram);
				break;
			case DspAlu::mul:
				a.mov(acc, mul);
				break;
			case DspAlu::negAcc:
				a.neg(acc);
				break;
			case DspAlu::iramMinusAcc:
				a.neg(acc);
				a.add(acc, iram);
				break;
			case DspAlu::mulMinusAcc:
				a.neg(acc);
				a.add(acc, mul);
				break;
			case DspAlu::iramPlusMul:
				a.mov(acc, iram);
				a.add(acc, mul);
				break;
			case DspAlu::minAccIram:
				a.cmp(acc, iram);
				a.cmovg(acc, iram);
				break;
			case DspAlu::maxAccIram:
				a.cmp(acc, iram);
				a.cmovl(acc, iram);
				break;
			case DspAlu::accPlusMulShr13:
				a.mov(_param, mul);
				a.sar(_param, 13);
				a.add(acc, _param);
				break;
			case DspAlu::mulShr13:
				a.mov(acc, mul);
				a.sar(acc, 13);
				break;
			case DspAlu::andImm:
				a.and_(acc, 0x1fffffff);
				a.and_(acc, _param);
				break;
			case DspAlu::orImm:
				a.and_(acc, 0x1fffffff);
				a.or_(acc, _param);
				break;
			case DspAlu::xorImm:
				a.and_(acc, 0x1fffffff);
				a.xor_(acc, _param);
				break;
			case DspAlu::minImm:
				a.cmp(acc, _param);
				a.cmovg(acc, _param);
				break;
			case DspAlu::maxImm:
				a.cmp(acc, _param);
				a.cmovl(acc, _param);
				break;
			case DspAlu::sameSignMinElseMax:
			case DspAlu::sameSignMaxElseMin:
				{
					// The registers of iram and mul are free here: the Imm functions do not read them.
					const Gp minimum = m_t[1];
					const Gp maximum = m_t[2];
					a.mov(minimum, acc);
					a.mov(maximum, acc);
					a.cmp(acc, _param);
					a.cmovg(minimum, _param);
					a.cmovl(maximum, _param);
					a.xor_(_param, acc); // negative when the signs differ
					a.test(_param, _param);
					if (_alu == DspAlu::sameSignMinElseMax)
					{
						a.mov(acc, minimum);
						a.cmovs(acc, maximum);
					}
					else
					{
						a.mov(acc, maximum);
						a.cmovs(acc, minimum);
					}
					break;
				}
			case DspAlu::accPlusImm:
				a.add(acc, _param);
				break;
			case DspAlu::iramPlusImm:
				a.mov(acc, iram);
				a.add(acc, _param);
				break;
			case DspAlu::mulPlusImm:
				a.mov(acc, mul);
				a.add(acc, _param);
				break;
			case DspAlu::negAccPlusImm:
				a.neg(acc);
				a.add(acc, _param);
				break;
			case DspAlu::accPlusMulPlusIram:
				a.add(acc, mul);
				a.add(acc, iram);
				break;
			case DspAlu::iramPlusMulMinusAcc:
				a.neg(acc);
				a.add(acc, iram);
				a.add(acc, mul);
				break;
			// The parallel D-F functions also consume the discarded negative fraction of the preceding multiply.
			case DspAlu::accPlusMulMinusIram:
				a.add(acc, mul);
				a.sub(acc, iram);
				a.movzx(_param.r32(), st1(_chip, offsetof(DspState, multiplyNegativeFraction)));
				a.sub(acc, _param);
				break;
			case DspAlu::mulMinusIramMinusAcc:
				a.neg(acc);
				a.add(acc, mul);
				a.sub(acc, iram);
				a.movzx(_param.r32(), st1(_chip, offsetof(DspState, multiplyNegativeFraction)));
				a.add(_param, _param);
				a.sub(acc, _param);
				break;
			case DspAlu::mulMinusIram:
				a.mov(acc, mul);
				a.sub(acc, iram);
				a.movzx(_param.r32(), st1(_chip, offsetof(DspState, multiplyNegativeFraction)));
				a.add(_param, _param);
				a.sub(acc, _param);
				break;
			}
			if (_wrap)
				sext(acc, 29);
			put(_chip.acc, acc);
		}

		void emitTransform(Chip& _chip, const DspTransform _transform, const bool _sext24)
		{
			auto& a = *m_asm;
			const Gp acc = get(_chip.acc, m_t[3]);
			bool narrowed = false;
			switch (_transform)
			{
			case DspTransform::absolute:
				a.mov(m_t[0], acc);
				a.sar(m_t[0], 63);
				a.xor_(acc, m_t[0]);
				a.sub(acc, m_t[0]);
				break;
			case DspTransform::sext24:
				sext(acc, 24);
				narrowed = true;
				break;
			case DspTransform::lfsr:
				a.mov(m_t[0], acc);
				a.sar(m_t[0], 23);
				a.mov(m_t[1], acc);
				a.sar(m_t[1], 6);
				a.xor_(m_t[0], m_t[1]);
				a.mov(m_t[1], acc);
				a.sar(m_t[1], 1);
				a.xor_(m_t[0], m_t[1]);
				a.and_(m_t[0], 1);
				a.shl(acc, 1);
				a.or_(acc, m_t[0]);
				break;
			case DspTransform::foldMirror:
				a.and_(acc, 0xffffff);
				a.mov(m_t[0], acc);
				a.shr(m_t[0], 1);
				a.xor_(m_t[0], acc);
				a.mov(m_t[1], acc);
				a.xor_(m_t[1], 0x7fffff);
				a.test(m_t[0], 0x400000);
				a.cmovnz(acc, m_t[1]);
				sext(acc, 24);
				narrowed = true;
				break;
			case DspTransform::onesComplementNegative:
				sext(acc, 24);
				a.mov(m_t[0], acc);
				a.not_(m_t[0]);
				a.test(acc, acc);
				a.cmovs(acc, m_t[0]);
				narrowed = true;
				break;
			default:
				break;
			}
			if (_sext24)
			{
				sext(acc, 24);
				narrowed = true;
			}
			if (!narrowed)
				sext(acc, 29);
			put(_chip.acc, acc);
		}

		void emitConsume(Chip& _chip, const size_t _port)
		{
			namespace x86 = asmjit::x86;
			auto& a = *m_asm;
			const auto skip = a.newLabel();
			a.mov(m_t[0].r32(), st4(_chip, offsetof(DspState, serialInputIndex) + _port * 4));
			a.cmp(m_t[0].r32(), st4(_chip, offsetof(DspState, serialInputCount) + _port * 4));
			a.jae(skip);
			a.movsxd(m_t[1], x86::dword_ptr(_chip.state, m_t[0], 2, off(offsetof(DspState, serialInput) + _port * dsp::nSerialWords * 4)));
			put(_port == 0 ? _chip.nodeA : _chip.nodeB, m_t[1]);
			a.inc(m_t[0].r32());
			a.mov(st4(_chip, offsetof(DspState, serialInputIndex) + _port * 4), m_t[0].r32());
			a.bind(skip);
		}

		void emitQueueRead(Chip& _chip, const Gp& _value, const int _countdown)
		{
			auto& a = *m_asm;
			const auto done = a.newLabel();
			for (int e = 0; e < 2; ++e)
			{
				const auto next = a.newLabel();
				a.cmp(st4(_chip, queueOffset(e, false)), 0);
				a.jge(next);
				a.mov(st4(_chip, queueOffset(e, true)), _value.r32());
				a.mov(st4(_chip, queueOffset(e, false)), _countdown);
				a.jmp(done);
				a.bind(next);
			}
			a.bind(done);
		}

		void emitOp(Chip& _chip, const FlatOp& _op, const size_t _pc)
		{
			namespace x86 = asmjit::x86;
			auto& a = *m_asm;
			const auto i = static_cast<size_t>(_chip.index);
			switch (_op.kind)
			{
			case FlatOpKind::eramArm:
				put(_chip.wv, get(_op.a ? _chip.iram : _chip.acc, m_t[0]));
				break;
			case FlatOpKind::eramWrite:
				{
					loadEramOffset(_chip, m_t[0], _op.index);
					const auto cell = eramCell(_chip, m_t[0], m_t[1]);
					encode24(m_t[2], get(_chip.wv, m_t[2]), m_t[1]);
					a.mov(cell, m_t[2].r32());
					break;
				}
			case FlatOpKind::eramLand:
				loadEramOffset(_chip, m_t[0], _op.index);
				loadEram(m_t[2], eramCell(_chip, m_t[0], m_t[1]));
				put(_chip.eram, m_t[2]);
				a.mov(st4(_chip, queueOffset(_op.a, true)), m_t[2].r32());
				break;
			case FlatOpKind::eramIndexed:
				a.mov(m_t[0], get(_chip.acc, m_t[0]));
				a.shr(m_t[0], 12);
				a.and_(m_t[0].r32(), 0xffff);
				a.mov(st2(_chip, offsetof(DspState, eramIndexedOffset)), m_t[0].r16());
				loadEram(m_t[2], eramCell(_chip, m_t[0], m_t[1]));
				a.mov(st4(_chip, queueOffset(_op.a, true)), m_t[2].r32());
				break;
			case FlatOpKind::eramLandIndexed:
				a.movsxd(m_t[0], st4(_chip, queueOffset(_op.a, true)));
				put(_chip.eram, m_t[0]);
				break;
			case FlatOpKind::eramGeneric:
				{
					const auto notPending = a.newLabel();
					const auto end = a.newLabel();
					a.cmp(st1(_chip, offsetof(DspState, eramPrefixPending)), 0);
					a.je(notPending);
					a.movzx(m_t[0].r32(), st1(_chip, offsetof(DspState, eramOffsetHigh)));
					a.shl(m_t[0].r32(), 9);
					a.movzx(m_t[1].r32(), x86::word_ptr(_chip.params, off(offsetof(DspParams, eramOffsetLow) + _op.index * 2)));
					a.or_(m_t[0].r32(), m_t[1].r32());
					const auto cell = eramCell(_chip, m_t[0], m_t[1]);
					{
						const auto read = a.newLabel();
						const auto consumed = a.newLabel();
						a.cmp(st1(_chip, offsetof(DspState, eramPendingWrite)), 0);
						a.je(read);
						encode24(m_t[2], get(_chip.wv, m_t[2]), m_t[1]);
						a.mov(cell, m_t[2].r32());
						a.jmp(consumed);
						a.bind(read);
						loadEram(m_t[2], cell);
						emitQueueRead(_chip, m_t[2], 1);
						a.bind(consumed);
					}
					a.mov(st1(_chip, offsetof(DspState, eramPrefixPending)), 0);
					a.mov(fr1(offsetof(DspJitFrame, portBusy) + i), 1);
					a.jmp(end);
					a.bind(notPending);
					a.mov(fr1(offsetof(DspJitFrame, portBusy) + i), 0);
					if (_op.a != static_cast<uint8_t>(DspEramArm::none))
					{
						a.mov(st1(_chip, offsetof(DspState, eramPrefixPending)), 1);
						a.mov(st1(_chip, offsetof(DspState, eramPendingWrite)),
							  _op.a != static_cast<uint8_t>(DspEramArm::read) ? 1 : 0);
						a.movzx(m_t[0].r32(), x86::byte_ptr(_chip.params, off(offsetof(DspParams, eramOffsetHigh) + _op.index)));
						a.mov(st1(_chip, offsetof(DspState, eramOffsetHigh)), m_t[0].r8());
						put(_chip.wv, get(_op.a == static_cast<uint8_t>(DspEramArm::writeIramLatch) ? _chip.iram : _chip.acc, m_t[0]));
					}
					a.bind(end);
					break;
				}
			case FlatOpKind::eramAdvance:
				for (int e = 0; e < 2; ++e)
				{
					const auto next = a.newLabel();
					a.mov(m_t[0].r32(), st4(_chip, queueOffset(e, false)));
					a.test(m_t[0].r32(), m_t[0].r32());
					a.js(next);
					a.dec(m_t[0].r32());
					a.mov(st4(_chip, queueOffset(e, false)), m_t[0].r32());
					a.jnz(next);
					a.movsxd(m_t[1], st4(_chip, queueOffset(e, true)));
					put(_chip.eram, m_t[1]);
					a.mov(st4(_chip, queueOffset(e, false)), -1);
					a.bind(next);
				}
				break;
			case FlatOpKind::eramIndexedGeneric:
				{
					const auto skip = a.newLabel();
					a.cmp(fr1(offsetof(DspJitFrame, portBusy) + i), 0);
					a.jne(skip);
					a.mov(m_t[0], get(_chip.acc, m_t[0]));
					a.shr(m_t[0], 12);
					a.and_(m_t[0].r32(), 0xffff);
					a.mov(st2(_chip, offsetof(DspState, eramIndexedOffset)), m_t[0].r16());
					loadEram(m_t[2], eramCell(_chip, m_t[0], m_t[1]));
					emitQueueRead(_chip, m_t[2], 2);
					a.bind(skip);
					break;
				}
			case FlatOpKind::iramWriteAcc:
				encode24(m_t[0], get(_chip.acc, m_t[0]), m_t[1]);
				a.mov(iramCell(_chip, _op.a, _op.b, m_t[2]), m_t[0].r32());
				break;
			case FlatOpKind::iramWriteLatch:
				a.mov(m_t[0], get(_chip.eram, m_t[0]));
				a.and_(m_t[0], 0xffffff);
				a.mov(iramCell(_chip, _op.a, _op.b, m_t[2]), m_t[0].r32());
				break;
			case FlatOpKind::iramRead:
				a.mov(m_t[0].r32(), iramCell(_chip, _op.a, _op.b, m_t[2]));
				sext(m_t[0], 24);
				put(_chip.iram, m_t[0]);
				break;
			case FlatOpKind::iramReadParam:
				a.mov(m_t[0].r32(), st4(_chip, offsetof(DspState, iram3) + _op.b * 4));
				a.shr(m_t[0].r32(), 10);
				a.mov(st2(_chip, offsetof(DspState, iram3ParameterLatch)), m_t[0].r16());
				break;
			case FlatOpKind::emitA:
				a.mov(m_t[0].r32(), x86::dword_ptr(get(_chip.procBank, m_t[2]), _op.b * 4));
				sext(m_t[0], 24);
				a.mov(st4(_chip, offsetof(DspState, serialOutput) + _op.a * 4), m_t[0].r32());
				break;
			case FlatOpKind::emitBcd:
				a.mov(m_t[0].r32(), x86::dword_ptr(get(_chip.procBank, m_t[2]), _op.b * 4));
				sext(m_t[0], 24);
				if (_op.c != 0xff)
					a.mov(st4(_chip, offsetof(DspState, serialOutput) + (1 + _op.a) * dsp::nSerialWords * 4 + _op.c * 4), m_t[0].r32());
				break;
			case FlatOpKind::emitADynamic:
				{
					a.mov(m_t[0].r32(), st4(_chip, offsetof(DspState, outputWordPosition)));
					a.mov(m_t[1].r32(), m_t[0].r32());
					a.and_(m_t[1].r32(), dsp::nIramSlots - 1);
					a.mov(m_t[1].r32(), x86::dword_ptr(get(_chip.procBank, m_t[2]), m_t[1], 2));
					sext(m_t[1], 24);
					a.inc(m_t[0].r32());
					a.mov(st4(_chip, offsetof(DspState, outputWordPosition)), m_t[0].r32());
					if (_op.a)
					{
						const auto skip = a.newLabel();
						a.mov(m_t[2].r32(), st4(_chip, offsetof(DspState, serialOutputCount)));
						a.cmp(m_t[2].r32(), dsp::nSerialWords);
						a.jae(skip);
						a.mov(x86::dword_ptr(_chip.state, m_t[2], 2, off(offsetof(DspState, serialOutput))), m_t[1].r32());
						a.inc(m_t[2].r32());
						a.mov(st4(_chip, offsetof(DspState, serialOutputCount)), m_t[2].r32());
						if (_chip.peerListens)
						{
							a.mov(fr4(offsetof(DspJitFrame, linkWord) + i * sizeof(int32_t)), m_t[1].r32());
							a.mov(fr1(offsetof(DspJitFrame, linkFlag) + i), 1);
						}
						a.bind(skip);
					}
					break;
				}
			case FlatOpKind::emitBcdDynamic:
				{
					const Gp port = m_t[2];
					const Gp value = m_t[1];
					a.movzx(port.r32(), st1(_chip, offsetof(DspState, dacPortPosition)));
					a.mov(m_t[0].r32(), st4(_chip, offsetof(DspState, outputWordPosition)));
					a.mov(value.r32(), m_t[0].r32());
					a.and_(value.r32(), dsp::nIramSlots - 1);
					a.mov(value.r32(), x86::dword_ptr(get(_chip.procBank, m_t[3]), value, 2));
					sext(value, 24);
					a.inc(m_t[0].r32());
					a.mov(st4(_chip, offsetof(DspState, outputWordPosition)), m_t[0].r32());
					if (_op.a != 0)
					{
						const auto skipStore = a.newLabel();
						const auto noBus = skipStore;
						a.mov(m_t[0].r32(), _op.a);
						a.bt(m_t[0].r32(), port.r32());
						a.jnc(skipStore);
						a.mov(m_t[0].r32(), x86::dword_ptr(_chip.state, port, 2, off(offsetof(DspState, serialOutputCount) + 4)));
						a.cmp(m_t[0].r32(), dsp::nSerialWords);
						a.jae(noBus);
						a.mov(m_t[3], port);
						a.shl(m_t[3], 7);
						a.add(m_t[3], _chip.state);
						a.mov(x86::dword_ptr(m_t[3], m_t[0], 2, off(offsetof(DspState, serialOutput) + dsp::nSerialWords * 4)), value.r32());
						a.inc(m_t[0].r32());
						a.mov(x86::dword_ptr(_chip.state, port, 2, off(offsetof(DspState, serialOutputCount) + 4)), m_t[0].r32());
						a.bind(skipStore);
					}
					a.inc(port.r32());
					a.xor_(m_t[0].r32(), m_t[0].r32());
					a.cmp(port.r32(), 3);
					a.cmove(port.r32(), m_t[0].r32());
					a.mov(st1(_chip, offsetof(DspState, dacPortPosition)), port.r8());
					break;
				}
			case FlatOpKind::pins:
				a.movzx(m_t[0].r32(), st1(_chip, offsetof(DspState, outputPins)));
				if ((_op.a & 1) == 0)
				{
					a.mov(m_t[1].r32(), m_t[0].r32());
					a.or_(m_t[1].r32(), 4);
					a.test(m_t[0].r32(), 1);
					a.cmovnz(m_t[0].r32(), m_t[1].r32());
				}
				a.and_(m_t[0].r32(), 4);
				if (_op.a != 0)
					a.or_(m_t[0].r32(), _op.a);
				a.mov(st1(_chip, offsetof(DspState, outputPins)), m_t[0].r8());
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
					a.cmp(st1(_chip, offsetof(DspState, dacPortPosition)), 1);
					a.jne(skip);
					emitConsume(_chip, 1);
					a.bind(skip);
					break;
				}
			case FlatOpKind::mulCapture:
				multiplySource(_chip, m_t[0], static_cast<DspMultiplyInput>(_op.a));
				capSet(0, m_t[0]);
				if (_op.b != _op.a)
					multiplySource(_chip, m_t[0], static_cast<DspMultiplyInput>(_op.b));
				capSet(1, m_t[0]);
				break;
			case FlatOpKind::factorCapture:
				{
					const auto kind = static_cast<DspMultiplyFactor>(_op.a);
					const Gp factor = m_t[1];
					if (kind != DspMultiplyFactor::iram3Parameter)
						sat24(m_t[0], get(_chip.acc, m_t[0]), m_t[2]);
					switch (kind)
					{
					case DspMultiplyFactor::accLow12Shl3:
						a.mov(factor, m_t[0]);
						a.and_(factor, 0xfff);
						a.shl(factor, 3);
						break;
					case DspMultiplyFactor::accBits22to8:
						a.mov(factor, m_t[0]);
						a.and_(factor, 0x7fffff);
						a.shr(factor, 8);
						break;
					case DspMultiplyFactor::accShr8:
						a.mov(factor, m_t[0]);
						a.sar(factor, 8);
						break;
					default:
						a.movsx(factor, st2(_chip, offsetof(DspState, iram3ParameterLatch)));
						break;
					}
					if (_op.b)
					{
						a.mov(m_t[0], 0x7fff);
						a.sub(m_t[0], factor);
						a.mov(factor, m_t[0]);
						if (kind >= DspMultiplyFactor::accShr8)
							a.sub(factor, 0x8000);
					}
					capSet(2, factor);
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
				capGet(0, m_t[1]);
				a.imul(m_t[0], m_t[1]);
				emitNegativeFraction(_chip, m_t[0], 13);
				divideTruncating(m_t[0], 13, m_t[1]);
				sat29(m_t[0], m_t[0], m_t[1]);
				put(_chip.mul, m_t[0]);
				capGet(1, m_t[1]);
				put(_chip.fb, m_t[1]);
				break;
			case FlatOpKind::mulFactor:
				capGet(0, m_t[0]);
				capGet(2, m_t[1]);
				a.imul(m_t[0], m_t[1]);
				if (_op.a != 0)
					a.shl(m_t[0], _op.a);
				emitNegativeFraction(_chip, m_t[0], 15);
				divideTruncating(m_t[0], 15, m_t[1]);
				sat29(m_t[0], m_t[0], m_t[1]);
				put(_chip.mul, m_t[0]);
				capGet(1, m_t[1]);
				put(_chip.fb, m_t[1]);
				break;
			case FlatOpKind::branch:
				{
					if (m_jumpsMode)
					{
						m_pendingBranch = true;
						m_pendingCondition = static_cast<DspBranch>(_op.a);
						m_pendingTarget = _op.index;
						break;
					}
					a.mov(m_t[0].r32(), static_cast<uint32_t>(_pc + 1));
					const auto branch = static_cast<DspBranch>(_op.a);
					if (branch == DspBranch::always)
						a.mov(m_t[0].r32(), static_cast<uint32_t>(_op.index));
					else
					{
						a.mov(m_t[1].r32(), static_cast<uint32_t>(_op.index));
						a.cmp(get(_chip.acc, m_t[2]), 0);
						switch (branch)
						{
						case DspBranch::eq0: a.cmove(m_t[0].r32(), m_t[1].r32()); break;
						case DspBranch::ne0: a.cmovne(m_t[0].r32(), m_t[1].r32()); break;
						case DspBranch::ge0: a.cmovge(m_t[0].r32(), m_t[1].r32()); break;
						case DspBranch::lt0: a.cmovl(m_t[0].r32(), m_t[1].r32()); break;
						case DspBranch::gt0: a.cmovg(m_t[0].r32(), m_t[1].r32()); break;
						default: a.cmovle(m_t[0].r32(), m_t[1].r32()); break;
						}
					}
					a.mov(fr4(offsetof(DspJitFrame, pc) + i * sizeof(uint32_t)), m_t[0].r32());
					m_pcNextWritten = true;
					break;
				}
			}
		}

		asmjit::JitRuntime m_runtime;
		DspJitRun m_run = nullptr;
		asmjit::Error m_lastError = asmjit::kErrorOk;

		asmjit::x86::Builder* m_asm = nullptr;
		std::vector<Chip> m_chips;
		std::vector<Gp> m_pool;
		size_t m_poolNext = 0;
		Gp m_t[4];
		bool m_pcNextWritten = false;
		bool m_jumpsMode = false;
		bool m_pendingBranch = false;
		DspBranch m_pendingCondition = DspBranch::never;
		uint16_t m_pendingTarget = 0;
		asmjit::Label m_jumpsExit;
	};
} // namespace xpLib
