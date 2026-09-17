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
 * Adapted for Gearmulator's sample-at-a-time device API.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "mt32reverb_common.h"

namespace mt32ReverbLib
{
	class Jit;

	// The chip as the board sees it: a microcode ROM, three parameter lines and a stereo frame
	// in and out. Loading the ROM compiles all of its programs (mt32reverb_jit.h); selecting the
	// parameters picks one. The interpreter (mt32reverb_interpreter.h) runs where there is no
	// backend or its compile failed.
	class Mt32Reverb
	{
	public:
		Mt32Reverb();
		explicit Mt32Reverb(std::vector<uint8_t> _rom);
		~Mt32Reverb();

		Mt32Reverb(Mt32Reverb&&) noexcept;
		Mt32Reverb& operator=(Mt32Reverb&&) noexcept;

		bool setRom(std::vector<uint8_t> _rom);
		bool isValid() const { return m_rom.size() == 0x4000 || m_rom.size() == 0x8000; }
		void reset();
		void setParameters(unsigned _mode, unsigned _time, unsigned _level);
		std::pair<int32_t, int32_t> renderFrame(std::pair<int32_t, int32_t> _input);

		// True while the selected program runs as compiled code rather than through the interpreter.
		bool jitActive() const { return m_run != nullptr; }
		const State& state() const { return m_state; }

	private:
		void selectProgram();

		std::vector<uint8_t> m_rom;
		State m_state;
		unsigned m_romBase = 0;
		std::unique_ptr<Jit> m_jit;
		void (*m_run)(State*) = nullptr;
	};
}
