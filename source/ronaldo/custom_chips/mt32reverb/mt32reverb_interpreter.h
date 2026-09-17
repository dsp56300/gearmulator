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

		static void update(Regs& _r, const uint8_t _control, const unsigned _mask)
		{
			if ((_control & 0x10) == 0) _r.accumulator = 0;
			if (_mask & _r.sawBits)
			{
				const int sum = int(_r.accumulator) + int(_r.shifter) + _r.carry;
				_r.accumulator = static_cast<int16_t>(std::clamp(sum, -0x8000, 0x7fff));
			}
			if (_control & 8) _r.accumulator = static_cast<int16_t>(~_r.accumulator);
		}

		static void cycle(const uint8_t* _program, const unsigned _step, State& _s, Regs& _r, const int16_t _input)
		{
			const uint8_t* rom = _program + _step * StepBytes;
			const unsigned ramAddress = (_s.position + (rom[0] | (unsigned(rom[2]) << 8))) & RamMask;
			uint8_t control = rom[1];
			int16_t nextShifter = _r.shifter >> 1;
			int16_t nextCarry = _r.shifter < 0 ? _r.shifter & 1 : 0;
			if ((control & 2) == 0)
			{
				const int16_t value = (control & 1) ? _input : _r.accumulator;
				_s.ram[ramAddress] = value;
				if ((control & 4) == 0) { nextShifter = value; _r.carry = nextCarry = 0; }
			}
			else if ((control & 4) == 0) { nextShifter = _s.ram[ramAddress]; _r.carry = nextCarry = 0; }
			update(_r, control, ((control >> 4) & 0x0e) | (rom[2] >> 7));
			_r.carry = nextCarry;
			_r.shifter = nextShifter;
			control = rom[3];
			update(_r, control, ((control >> 4) & 0x0e) | ((rom[2] >> 6) & 1));
			_r.carry = _r.shifter < 0 ? _r.shifter & 1 : 0;
			_r.shifter >>= 1;
		}
	};
}
