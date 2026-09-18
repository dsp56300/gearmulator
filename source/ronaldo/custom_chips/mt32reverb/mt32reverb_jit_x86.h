#pragma once

#include <cstddef>
#include <vector>

#include <asmjit/asmjit.h>
#include <asmjit/x86.h>

#include "mt32reverb_common.h"

namespace mt32ReverbLib
{
	// x86-64 backend, the mirror of mt32reverb_jit_arm64.h. The state image is reached through
	// rcx (the Windows argument register; the SysV one is moved into it first), the carried
	// registers live in r8d-r11d for the whole frame, and rbx / rbp are the only callee-saved
	// registers touched. The saw selector is tested straight from memory with an immediate mask.
	class Jit
	{
	public:
		using Run = void (*)(State*);

		Jit() = default;
		~Jit() { release(); }

		Jit(const Jit&) = delete;
		Jit& operator=(const Jit&) = delete;

		// Compile every program of a ROM image. Nothing is kept if one of them fails.
		bool compile(const uint8_t* _rom, const size_t _size)
		{
			release();
			const size_t count = _size / ProgramBytes;
			m_runs.reserve(count);
			for (size_t i = 0; i < count; ++i)
			{
				Run run = nullptr;
				if (!compileProgram(_rom + i * ProgramBytes, run))
				{
					release();
					return false;
				}
				m_runs.push_back(run);
			}
			return true;
		}

		void release()
		{
			for (const auto run : m_runs)
				m_jitRuntime.release(run);
			m_runs.clear();
		}

		size_t programCount() const { return m_runs.size(); }
		Run program(const size_t _index) const { return _index < m_runs.size() ? m_runs[_index] : nullptr; }

	private:
		struct ErrorSink final : asmjit::ErrorHandler
		{
			bool failed = false;
			void handleError(asmjit::Error, const char*, asmjit::BaseEmitter*) override { failed = true; }
		};

		static constexpr auto regState = asmjit::x86::rcx;
		static constexpr auto regPos   = asmjit::x86::r8d;
		static constexpr auto regAcc   = asmjit::x86::r9d;
		static constexpr auto regShift = asmjit::x86::r10d;
		static constexpr auto regCarry = asmjit::x86::r11d;
		static constexpr auto tAddr    = asmjit::x86::rax;	// RAM index, zero-extended by the mask
		static constexpr auto tSum     = asmjit::x86::edx;
		static constexpr auto tConst   = asmjit::x86::ebx;	// saturation bound for cmov
		static constexpr auto tNext    = asmjit::x86::ebp;	// the shifter's next value on a load

		static asmjit::x86::Mem st(const size_t _offset)
		{
			return asmjit::x86::dword_ptr(regState, static_cast<int32_t>(_offset));
		}

		bool compileProgram(const uint8_t* _program, Run& _run)
		{
			namespace x86 = asmjit::x86;

			ErrorSink errors;
			asmjit::CodeHolder code;
			code.init(m_jitRuntime.environment());
			code.setErrorHandler(&errors);
			x86::Assembler a(&code);
			m_asm = &a;

#if !defined(_WIN32)
			a.mov(regState, x86::rdi);
#endif
			a.push(x86::rbx);
			a.push(x86::rbp);

			a.mov(regPos, st(offsetof(State, position)));
			a.mov(regAcc, st(offsetof(State, accumulator)));
			a.mov(regShift, st(offsetof(State, shifter)));
			a.mov(regCarry, st(offsetof(State, carry)));

			for (unsigned step = 0; step < StepCount; ++step)
				emitStep(decodeStep(_program, step), step);

			a.inc(regPos);
			a.and_(regPos, RamMask);
			a.mov(st(offsetof(State, position)), regPos);
			a.mov(st(offsetof(State, accumulator)), regAcc);
			a.mov(st(offsetof(State, shifter)), regShift);
			a.mov(st(offsetof(State, carry)), regCarry);

			a.pop(x86::rbp);
			a.pop(x86::rbx);
			a.ret();

			m_asm = nullptr;
			if (errors.failed)
				return false;
			return m_jitRuntime.add(&_run, &code) == asmjit::kErrorOk;
		}

		void emitStep(const Step& _s, const unsigned _step)
		{
			namespace x86 = asmjit::x86;
			auto& a = *m_asm;

			if (_step == OutRightStep)
				a.mov(st(offsetof(State, outRight)), regAcc);
			else if (_step == OutLeftStep)
				a.mov(st(offsetof(State, outLeft)), regAcc);

			const auto inputOffset = _step < InputSplit ? offsetof(State, inputRight) : offsetof(State, inputLeft);
			const bool write = _s.write();
			const bool load = _s.load();

			if (write || load)
			{
				a.lea(tAddr.r32(), x86::ptr(regPos.r64(), static_cast<int32_t>(_s.offset)));
				a.and_(tAddr.r32(), RamMask);
			}
			const auto ram = x86::word_ptr(regState, tAddr, 1, static_cast<int32_t>(offsetof(State, ram)));

			if (write)
			{
				auto value = regAcc;
				if (_s.useInput())
				{
					a.movsx(tSum, x86::word_ptr(regState, static_cast<int32_t>(inputOffset)));
					value = tSum;
				}
				a.mov(ram, value.r16());
				if (load)
					a.mov(tNext, value);
			}
			else if (load)
				a.movsx(tNext, ram);

			// A load clears the carry ahead of the first update and it stays clear for the
			// second; the tail recomputes it from the shifted value either way.
			emitUpdate(_s.control[0], _s.mask[0], load);
			if (load)
				a.mov(regShift, tNext);
			else
			{
				emitNextCarry();
				a.sar(regShift, 1);
			}
			emitUpdate(_s.control[1], _s.mask[1], load);
			emitNextCarry();
			a.sar(regShift, 1);
		}

		// carry = shifter < 0 ? shifter & 1 : 0
		void emitNextCarry()
		{
			m_asm->mov(regCarry, regShift);
			m_asm->shr(regCarry, 31);
			m_asm->and_(regCarry, regShift);
		}

		void emitClampHigh()
		{
			m_asm->cmp(tSum, 0x7fff);
			m_asm->mov(tConst, 0x7fff);
			m_asm->cmovg(tSum, tConst);
		}

		void emitClampLow()
		{
			m_asm->cmp(tSum, -0x8000);
			m_asm->mov(tConst, -0x8000);
			m_asm->cmovl(tSum, tConst);
		}

		// One half-step: clear, conditional shift-add with 16-bit saturation, negate. The saw
		// test is a runtime one only for the masks that neither always nor never hit.
		void emitUpdate(const uint8_t _control, const uint8_t _mask, const bool _carryZero)
		{
			auto& a = *m_asm;
			const bool clear = Step::clear(_control);

			if (_mask == 0)
			{
				if (clear)
					a.xor_(regAcc, regAcc);
			}
			else
			{
				auto sum = tSum;
				if (clear)
				{
					if (_carryZero)
						sum = regShift;				// already a 16-bit value
					else
					{
						a.mov(tSum, regShift);
						a.add(tSum, regCarry);
						emitClampHigh();
					}
				}
				else
				{
					a.mov(tSum, regAcc);
					a.add(tSum, regShift);
					if (!_carryZero)
						a.add(tSum, regCarry);
					emitClampHigh();
					emitClampLow();
				}

				if (_mask == 0x0f)
					a.mov(regAcc, sum);
				else
				{
					if (clear)
						a.xor_(regAcc, regAcc);	// before the test: xor writes the flags
					a.test(st(offsetof(State, sawBits)), _mask);
					a.cmovnz(regAcc, sum);
				}
			}

			if (Step::negate(_control))
				a.not_(regAcc);
		}

		asmjit::JitRuntime m_jitRuntime;
		std::vector<Run>   m_runs;
		asmjit::x86::Assembler* m_asm = nullptr;
	};
}
