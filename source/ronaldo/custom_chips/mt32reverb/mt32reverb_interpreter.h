/* Copyright (C) 2013, 2014 Sergey V. Mikayev
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
 * License for more details.
 *
 * Derived from Sergey V. Mikayev's HG61H20R36F/BOS-007 emulator.
 */
#pragma once

#include <algorithm>

#include "mt32reverb_common.h"

namespace mt32ReverbLib
{
	// The reference engine: one frame of one program, step by step. This is what the JIT
	// backends are checked against, and the path taken where no backend exists.
	class Interpreter
	{
	public:
		static void renderFrame(const uint8_t* _program, State& _s)
		{
			Regs r;
			r.accumulator = static_cast<int16_t>(_s.accumulator);
			r.shifter = static_cast<int16_t>(_s.shifter);
			r.carry = static_cast<int16_t>(_s.carry);
			r.sawBits = static_cast<unsigned>(_s.sawBits);

			const auto left = static_cast<int16_t>(_s.inputLeft);
			const auto right = static_cast<int16_t>(_s.inputRight);

			for (unsigned step = 0; step < StepCount; ++step)
			{
				if (step == OutRightStep) _s.outRight = r.accumulator;
				else if (step == OutLeftStep) _s.outLeft = r.accumulator;
				cycle(_program, step, _s, r, step < InputSplit ? right : left);
			}
			_s.position = static_cast<int32_t>((_s.position + 1) & RamMask);

			_s.accumulator = r.accumulator;
			_s.shifter = r.shifter;
			_s.carry = r.carry;
		}

	private:
		struct Regs
		{
			int16_t accumulator = 0;
			int16_t shifter = 0;
			int16_t carry = 0;
			unsigned sawBits = 0;
		};

		// One half-step, in the order of the gate array:
		// the accumulator is cleared, or the shift-add runs if the saw selector is in the mask,
		// saturating and then optionally inverted; a clear adds nothing and inverts nothing.
		// Then the shifter shifts right or takes the step's RAM word, and the carry for the
		// next add is the old shifter's low bit, kept only while the new shifter is negative
		// (the rounding of the arithmetic shift, gated by the sign the shifter has now).
		static void half(Regs& _r, const uint8_t _control, const unsigned _mask, const int16_t _loaded)
		{
			if (Step::clear(_control))
				_r.accumulator = 0;
			else if (_mask & _r.sawBits)
			{
				int sum = std::clamp(int(_r.accumulator) + int(_r.shifter) + _r.carry, -0x8000, 0x7fff);
				if (Step::negate(_control))
					sum = ~sum;
				_r.accumulator = static_cast<int16_t>(sum);
			}
			const auto lsb = static_cast<int16_t>(_r.shifter & 1);
			_r.shifter = Step::load(_control) ? _loaded : static_cast<int16_t>(_r.shifter >> 1);
			_r.carry = _r.shifter < 0 ? lsb : 0;
		}

		static void cycle(const uint8_t* _program, const unsigned _step, State& _s, Regs& _r, const int16_t _input)
		{
			const Step step = decodeStep(_program, _step);
			const unsigned ramAddress = (_s.position + step.offset) & RamMask;
			if (step.write())
				_s.ram[ramAddress] = step.useInput() ? _input : _r.accumulator;
			const int16_t loaded = _s.ram[ramAddress];
			half(_r, step.control[0], step.mask[0], loaded);
			half(_r, step.control[1], step.mask[1], loaded);
		}
	};
}
