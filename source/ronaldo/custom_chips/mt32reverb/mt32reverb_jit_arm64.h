#pragma once

#include <cstddef>
#include <vector>

#include <asmjit/asmjit.h>
#include <asmjit/a64.h>

#include "mt32reverb_common.h"

namespace mt32ReverbLib
{
	// arm64 backend. Every 256-byte program of the microcode ROM becomes one straight-line
	// function `void(State*)`, compiled once when the ROM is loaded. The carried registers and
	// the saw selector live in w1-w7 for the whole frame, RAM is reached through x8, every step
	// constant is an immediate, and the state is loaded from and stored back to the State image
	// around the pass, so the interpreter can take over on any frame boundary. Only caller-saved
	// registers are used: there is no prologue to speak of.
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

		static constexpr auto regState = asmjit::a64::x0;	// the only argument
		static constexpr auto regPos   = asmjit::a64::w1;
		static constexpr auto regAcc   = asmjit::a64::w2;
		static constexpr auto regShift = asmjit::a64::w3;
		static constexpr auto regCarry = asmjit::a64::w4;
		static constexpr auto regSaw   = asmjit::a64::w5;
		static constexpr auto regInR   = asmjit::a64::w6;
		static constexpr auto regInL   = asmjit::a64::w7;
		static constexpr auto regRam   = asmjit::a64::x8;
		static constexpr auto tAddr    = asmjit::a64::x9;	// RAM index, zero-extended by the mask
		static constexpr auto tSum     = asmjit::a64::w10;
		static constexpr auto tMask    = asmjit::a64::w11;
		static constexpr auto tNext    = asmjit::a64::w12;	// the shifter's next value on a load
		static constexpr auto regMax   = asmjit::a64::w13;	// 0x7fff
		static constexpr auto regMin   = asmjit::a64::w14;	// -0x8000

		static asmjit::a64::Mem st(const size_t _offset)
		{
			return asmjit::a64::ptr(regState, static_cast<int32_t>(_offset));
		}

		bool compileProgram(const uint8_t* _program, Run& _run)
		{
			namespace a64 = asmjit::a64;

			ErrorSink errors;
			asmjit::CodeHolder code;
			code.init(m_jitRuntime.environment());
			code.setErrorHandler(&errors);
			a64::Assembler a(&code);
			m_asm = &a;

			a.ldr(regPos, st(offsetof(State, position)));
			a.ldr(regAcc, st(offsetof(State, accumulator)));
			a.ldr(regShift, st(offsetof(State, shifter)));
			a.ldr(regCarry, st(offsetof(State, carry)));
			a.ldr(regSaw, st(offsetof(State, sawBits)));
			a.ldr(regInR, st(offsetof(State, inputRight)));
			a.ldr(regInL, st(offsetof(State, inputLeft)));
			a.add(regRam, regState, static_cast<uint32_t>(offsetof(State, ram)));
			a.mov(regMax, 0x7fff);
			a.mov(regMin, -0x8000);

			for (unsigned step = 0; step < StepCount; ++step)
				emitStep(decodeStep(_program, step), step);

			a.add(regPos, regPos, 1);
			a.and_(regPos, regPos, RamMask);
			a.str(regPos, st(offsetof(State, position)));
			a.str(regAcc, st(offsetof(State, accumulator)));
			a.str(regShift, st(offsetof(State, shifter)));
			a.str(regCarry, st(offsetof(State, carry)));
			a.ret(a64::x30);

			m_asm = nullptr;
			if (errors.failed)
				return false;
			return m_jitRuntime.add(&_run, &code) == asmjit::kErrorOk;
		}

		void emitStep(const Step& _s, const unsigned _step)
		{
			namespace a64 = asmjit::a64;
			auto& a = *m_asm;

			if (_step == OutRightStep)
				a.str(regAcc, st(offsetof(State, outRight)));
			else if (_step == OutLeftStep)
				a.str(regAcc, st(offsetof(State, outLeft)));

			const auto input = _step < InputSplit ? regInR : regInL;
			const bool write = _s.write();
			const bool load = _s.load();

			if (write || load)
			{
				if (_s.offset < 0x1000)
					a.add(tAddr.w(), regPos, _s.offset);
				else
				{
					a.mov(tAddr.w(), _s.offset);
					a.add(tAddr.w(), tAddr.w(), regPos);
				}
				a.and_(tAddr.w(), tAddr.w(), RamMask);
			}
			const auto ram = a64::ptr(regRam, tAddr, a64::lsl(1));

			// The value the shifter takes after the first half-step, when it loads one.
			auto next = tNext;
			if (write)
			{
				const auto value = _s.useInput() ? input : regAcc;
				a.strh(value, ram);
				if (load)
				{
					if (_s.useInput())
						next = input;			// the input registers never change
					else
						a.mov(tNext, regAcc);	// the accumulator may, before the shifter takes it
				}
			}
			else if (load)
				a.ldrsh(tNext, ram);

			// A load clears the carry ahead of the first update and it stays clear for the
			// second; the tail recomputes it from the shifted value either way.
			emitUpdate(_s.control[0], _s.mask[0], load);
			if (load)
				a.mov(regShift, next);
			else
			{
				emitNextCarry();
				a.asr(regShift, regShift, 1);
			}
			emitUpdate(_s.control[1], _s.mask[1], load);
			emitNextCarry();
			a.asr(regShift, regShift, 1);
		}

		// carry = shifter < 0 ? shifter & 1 : 0
		void emitNextCarry()
		{
			m_asm->lsr(regCarry, regShift, 31);
			m_asm->and_(regCarry, regCarry, regShift);
		}

		// One half-step: clear, conditional shift-add with 16-bit saturation, negate. The saw
		// test is a runtime one only for the masks that neither always nor never hit.
		void emitUpdate(const uint8_t _control, const uint8_t _mask, const bool _carryZero)
		{
			namespace a64 = asmjit::a64;
			auto& a = *m_asm;
			const bool clear = Step::clear(_control);

			if (_mask == 0)
			{
				if (clear)
					a.mov(regAcc, 0);
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
						a.add(tSum, regShift, regCarry);
						a.cmp(tSum, regMax);
						a.csel(tSum, tSum, regMax, a64::CondCode::kLE);
					}
				}
				else
				{
					a.add(tSum, regAcc, regShift);
					if (!_carryZero)
						a.add(tSum, tSum, regCarry);
					a.cmp(tSum, regMax);
					a.csel(tSum, tSum, regMax, a64::CondCode::kLE);
					a.cmp(tSum, regMin);
					a.csel(tSum, tSum, regMin, a64::CondCode::kGE);
				}

				if (_mask == 0x0f)
					a.mov(regAcc, sum);
				else
				{
					a.mov(tMask, _mask);
					a.tst(tMask, regSaw);
					a.csel(regAcc, sum, clear ? a64::wzr : regAcc, a64::CondCode::kNE);
				}
			}

			if (Step::negate(_control))
				a.mvn(regAcc, regAcc);
		}

		asmjit::JitRuntime m_jitRuntime;
		std::vector<Run>   m_runs;
		asmjit::a64::Assembler* m_asm = nullptr;
	};
}
