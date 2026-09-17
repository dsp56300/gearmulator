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
#include "mt32reverb.h"

#include <algorithm>

#include "mt32reverb_interpreter.h"
#include "mt32reverb_jit.h"

namespace mt32ReverbLib
{
	Mt32Reverb::Mt32Reverb() = default;
	Mt32Reverb::Mt32Reverb(std::vector<uint8_t> _rom) { setRom(std::move(_rom)); }
	Mt32Reverb::~Mt32Reverb() = default;
	Mt32Reverb::Mt32Reverb(Mt32Reverb&&) noexcept = default;
	Mt32Reverb& Mt32Reverb::operator=(Mt32Reverb&&) noexcept = default;

	bool Mt32Reverb::setRom(std::vector<uint8_t> _rom)
	{
		if (_rom.size() != 0x4000 && _rom.size() != 0x8000) return false;
		m_rom = std::move(_rom);
		m_jit.reset();
		m_run = nullptr;
#if MT32REVERB_JIT
		auto jit = std::make_unique<Jit>();
		if (jit->compile(m_rom.data(), m_rom.size()))
			m_jit = std::move(jit);
#endif
		reset();
		return true;
	}

	void Mt32Reverb::reset()
	{
		m_state.reset();
		m_state.sawBits = 0;
		m_romBase = 0;
		selectProgram();
	}

	void Mt32Reverb::setParameters(unsigned _mode, const unsigned _time, const unsigned _level)
	{
		_mode &= m_rom.size() == 0x8000 ? 7 : 3;
		const unsigned base = (_mode << 12) | ((_level & 7) << 9) | ((_time & 4) << 6);
		if (base >= m_rom.size())
		{
			m_romBase = 0;
			m_state.sawBits = 0;
		}
		else
		{
			m_romBase = base;
			m_state.sawBits = 1 << (_time & 3);
		}
		selectProgram();
	}

	void Mt32Reverb::selectProgram()
	{
#if MT32REVERB_JIT
		m_run = m_jit ? m_jit->program(m_romBase / ProgramBytes) : nullptr;
#endif
	}

	std::pair<int32_t, int32_t> Mt32Reverb::renderFrame(const std::pair<int32_t, int32_t> _input)
	{
		if (!isValid()) return {};
		m_state.inputLeft = static_cast<int16_t>(std::clamp(_input.first, -32768, 32767) >> 1);
		m_state.inputRight = static_cast<int16_t>(std::clamp(_input.second, -32768, 32767) >> 1);
		if (m_run)
			m_run(&m_state);
		else
			Interpreter::renderFrame(m_rom.data() + m_romBase, m_state);
		return {m_state.outLeft, m_state.outRight};
	}
}
