
/*
 * Some parts of this code are derived from Nuked-SC55
 *
 * Copyright (C) 2021, 2024 nukeykt
 *
 * This file is part of Nuked-SC55.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "sc55mk2.h"
#include "baseLib/md5.h"

#include <cstring>

namespace emu88Lib
{
	namespace
	{
		// Page-0 window bases.
		constexpr uint16_t g_sramBase   = 0x8000;
		constexpr uint16_t g_gpBase     = 0xE000;
		constexpr uint16_t g_gpEnd      = 0xE3FF;
		constexpr uint16_t g_gaBase     = 0xE400;
		constexpr uint16_t g_gaEnd      = 0xE7FF;
		constexpr uint16_t g_subMcuBase = 0xEC00;
		constexpr uint16_t g_subMcuEnd  = 0xEFFF;

		// Gate-array registers, relative to g_gaBase.
		constexpr uint16_t g_gaIrqStatus = 0x02;	// 0xE402

		// Gate-array interrupt lines. The sub-MCU is the only one this board
		// wires; the GP's voice-end line bypasses the multiplexer entirely.
		constexpr uint8_t g_gaLineSubMcu = 5;

		// Rear-panel mode switch, as the A/D sees it.
		constexpr uint16_t g_switchLevels[4] = { 0x000, 0x155, 0x2aa, 0x3ff };
	}

	Sc55Mk2::Sc55Mk2(Sc55RomSet _roms)
		: m_roms(std::move(_roms))
	{
		// The on-chip ROM holds the reset vector, so without it there is
		// nothing to run. The program ROM is where almost all the firmware
		// lives; a board without it boots into garbage, so require both.
		m_valid = m_roms.internalRom.size() >= InternalRomSize
		       && !m_roms.programRom.empty();

		if(m_roms.programRom.size() > ProgramRomSize)
			m_roms.programRom.resize(ProgramRomSize);

		m_autoVoiceReset = baseLib::MD5(m_roms.internalRom) == baseLib::MD5("4ca058f7db05f51e97bb30a162e9610a")
			&& baseLib::MD5(m_roms.programRom) == baseLib::MD5("63b24c7193ce34afefce9cec32ac39f0");

		// The GP's two PCM mask ROMs sit on chip selects 0 and 1. The dumps
		// are raw, so undo the board's address/data line scramble first.
		for(uint8_t bank = 0; bank < 2; ++bank)
		{
			if(m_roms.waveRom[bank].empty())
				continue;
			descrambleWaveRom(m_roms.waveRom[bank]);
			m_gp.setWaveRom(bank, m_roms.waveRom[bank]);
		}

		// The GP's voice-end line goes straight to IRQ0; only the gate array's
		// own sources are multiplexed onto IRQ1.
		m_gp.setIrqCallback([this](const bool _level) { requestIrq(IrqGp, _level); });

		Sc55SubMcu::Hooks hooks;
		// The sub-MCU is gate-array line 5. It pulses the line 1 then 0; the
		// gate array latches the rising edge, so both halves are forwarded.
		hooks.hostIrq  = [this](const int _level) { setGateArrayInt(g_gaLineSubMcu, _level != 0); };
		hooks.midiOut  = [this](const uint8_t _b) { m_midiOut.push_back(_b); };
		// The panel matrix is board wiring: the firmware drives the columns
		// through the sub-MCU's P0 and reads the rows back on P1, and the
		// sub-MCU firmware itself never looks at either.
		hooks.writeP0  = [this](const uint8_t _v) { panelWrite(_v); };
		hooks.readP1   = [this]() { return panelRead(); };
		m_subMcu.setHooks(hooks);

		wireChip();
		m_machine.reset();
		m_cycleTarget = m_machine.now();
	}

	// =====================================================================
	// Address decode
	// =====================================================================

	void Sc55Mk2::descrambleWaveRom(std::vector<uint8_t>& _rom)
	{
		// Both tables are the reference model's, in its own convention:
		// output address bit j comes from input bit kAddr[j], and output data
		// bit j from input data bit kData[j].
		static constexpr int kAddr[20] =
			{ 2, 0, 3, 4, 1, 9, 13, 10, 18, 17, 6, 15, 11, 16, 8, 5, 12, 7, 14, 19 };
		static constexpr int kData[8] = { 2, 0, 4, 5, 7, 6, 3, 1 };

		std::vector<uint8_t> out(_rom.size());
		for(size_t i = 0; i < _rom.size(); ++i)
		{
			// Bits above 19 pass through, so the permutation repeats once per
			// 1 MiB chip rather than spanning a multi-chip image.
			size_t src = i & ~static_cast<size_t>(0xfffff);
			for(int j = 0; j < 20; ++j)
			{
				if(i & (static_cast<size_t>(1) << j))
					src |= static_cast<size_t>(1) << kAddr[j];
			}

			const uint8_t s = src < _rom.size() ? _rom[src] : 0;
			uint8_t d = 0;
			for(int j = 0; j < 8; ++j)
			{
				if(s & (1u << kData[j]))
					d |= static_cast<uint8_t>(1u << j);
			}
			out[i] = d;
		}
		_rom.swap(out);
	}

	uint32_t Sc55Mk2::romAddress(const uint32_t _addr)
	{
		// The board wires the ROM's high address line off CPU address bit 19
		// rather than continuing the linear sequence, so pages 8-9 and 14-15
		// reach the ROM's upper half while pages 1-4 reach the lower one.
		uint32_t a = _addr & 0x3FFFF;
		if(_addr & 0x80000)
			a |= 0x40000;
		return a;
	}

	uint8_t Sc55Mk2::extRead8(const uint32_t _addr)
	{
		const uint8_t v = extRead8Raw(_addr);
		return v;
	}

	uint8_t Sc55Mk2::extRead8Raw(const uint32_t _addr)
	{
		const uint8_t page = static_cast<uint8_t>(_addr >> 16);
		const uint16_t off = static_cast<uint16_t>(_addr);

		if(page == 0)
		{
			if(off < 0x8000)
			{
				// The H8/532's own 32 KiB mask ROM. Bounds-checked because a
				// board constructed without it still runs its reset sequence
				// — isValid() reports the problem, it does not prevent it.
				return off < m_roms.internalRom.size() ? m_roms.internalRom[off] : 0xFF;
			}
			if(off < g_gpBase)
				return m_sram[(off - g_sramBase) & (SramSize - 1)];
			if(off <= g_gpEnd)
				return m_gp.read8(off & 0x3F);
			if(off >= g_gaBase && off <= g_gaEnd)
				return gateArrayRead(static_cast<uint16_t>(off - g_gaBase));
			if(off >= g_subMcuBase && off <= g_subMcuEnd)
				return m_subMcu.hostRead(static_cast<uint8_t>(off & 0xFF));
			// 0xFB80-0xFFFF never gets here: the CPU decodes its own on-chip
			// RAM and SFR windows before calling us.
			return 0xFF;
		}

		switch(page)
		{
		case 1: case 2: case 3: case 4:
		case 8: case 9:
		case 14: case 15:
			{
				const uint32_t a = romAddress(_addr);
				return a < m_roms.programRom.size() ? m_roms.programRom[a] : 0xFF;
			}
		case 10: case 11:
			return m_sram[off & (SramSize - 1)];
		default:
			return 0xFF;
		}
	}

	void Sc55Mk2::extWrite8(const uint32_t _addr, const uint8_t _val)
	{

		const uint8_t page = static_cast<uint8_t>(_addr >> 16);
		const uint16_t off = static_cast<uint16_t>(_addr);

		if(page == 0)
		{
			if(off < 0x8000)
				return;	// mask ROM
			if(off < g_gpBase)
			{
				m_sram[(off - g_sramBase) & (SramSize - 1)] = _val;
				observeVoiceWrite((off - g_sramBase) & (SramSize - 1));
				return;
			}
			if(off <= g_gpEnd)
			{
				m_gp.write8(off & 0x3F, _val);
				if((off & 0x3f) < 4)
				{
					const auto shift = (3 - (off & 3)) * 8;
					m_pendingVoiceResets &= ~(uint32_t(static_cast<uint8_t>(~_val)) << shift);
				}
				return;
			}
			if(off >= g_gaBase && off <= g_gaEnd)
			{
				gateArrayWrite(static_cast<uint16_t>(off - g_gaBase), _val);
				return;
			}
			if(off >= g_subMcuBase && off <= g_subMcuEnd)
			{
				m_subMcu.hostWrite(static_cast<uint8_t>(off & 0xFF), _val);
				return;
			}
			return;
		}

		if(page == 10 || page == 11)
		{
			m_sram[off & (SramSize - 1)] = _val;
			observeVoiceWrite(off & (SramSize - 1));
		}
		// Everything else is ROM or unmapped.
	}

	// =====================================================================
	// Gate array
	// =====================================================================

	uint8_t Sc55Mk2::gateArrayRead(const uint16_t _addr)
	{
		if(_addr == g_gaIrqStatus)
		{
			// Reading the status reports which line fired and drops IRQ1 —
			// the firmware's only way to acknowledge. The value is the line
			// NUMBER, which is why line 0 is not usable as a source: zero is
			// how the register says "nothing pending".
			const uint8_t v = m_gaIntTrigger;
			m_gaIntTrigger = 0;
			requestIrq(IrqGateArray, false);
			return v;
		}
		return 0xFF;
	}

	void Sc55Mk2::gateArrayWrite(const uint16_t _addr, const uint8_t _val)
	{
		switch(_addr)
		{
		case 0x01:
			// Mode/scan register. Bit 0 blanks the display, and bits 2-3
			// select what the A/D's channel 7 is looking at (see analogRead).
			m_ioSd = _val;
			m_lcdEnabled = (_val & 1) == 0;
			break;
		case 0x02:
			// Interrupt enable, shifted up one: the gate array numbers its
			// lines from 1, the mask bit for line n is bit n.
			m_gaIrqEnable = static_cast<uint8_t>(_val << 1);
			break;
		case 0x04: m_lcd.write(false, _val); break;	// instruction register
		case 0x05: m_lcd.write(true,  _val); break;	// data register
		default: break;
		}
	}

	void Sc55Mk2::setGateArrayInt(const uint8_t _line, const bool _level)
	{
		if(_line >= m_gaInt.size())
			return;
		// A rising edge on an ENABLED line latches that line's number. The
		// enable mask comes from the firmware's write to 0xE402, shifted up
		// one; until it has written that, nothing can interrupt.
		if(_level && !m_gaInt[_line] && (m_gaIrqEnable & (1u << _line)))
			m_gaIntTrigger = _line;
		m_gaInt[_line] = _level;
		requestIrq(IrqGateArray, m_gaIntTrigger != 0);
	}

	// =====================================================================
	// Ports
	// =====================================================================

	uint8_t Sc55Mk2::portRead(const unsigned _port, const uint8_t _value)
	{
		if(_port == 9)
		{
			const uint8_t cfg = 0x02;
			const uint8_t dir = m_machine.ports().ddr(9);
			return static_cast<uint8_t>((cfg & static_cast<uint8_t>(~dir)) | (_value & dir));
		}
		return _value;
	}

	uint16_t Sc55Mk2::analogRead(const uint8_t _channel)
	{
		// Only channel 7 is wired, and what it looks at is multiplexed by the
		// gate array's mode register (0xE401) bits 3-2:
		//   0 = battery voltage, 1 = not connected, 2 = the rear-panel switch,
		//   3 = the remote-control unit, which this board does not model.
		// Every other channel floats. Getting this wrong is not subtle — the
		// firmware puts "Battery Low" on the display and refuses to go on.
		if(_channel != 7)
			return 0;

		switch((m_ioSd >> 2) & 3)
		{
		case 0:  return AnalogBattery;
		case 2:  return m_switchPosition < 4 ? g_switchLevels[m_switchPosition] : g_switchLevels[0];
		default: return 0;
		}
	}

	// =====================================================================
	// Front panel + MIDI
	// =====================================================================

	void Sc55Mk2::setButton(const Button _button, const bool _pressed)
	{
		setButton(static_cast<uint32_t>(_button), _pressed);
	}

	void Sc55Mk2::setButton(const uint32_t _index, const bool _pressed)
	{
		if(_index >= g_buttonCount)
			return;
		if(_pressed) m_buttons |=  (1u << _index);
		else         m_buttons &= ~(1u << _index);
	}

	void Sc55Mk2::panelWrite(const uint8_t _columns)
	{
		m_panelColumns = _columns;

		// P0 does double duty: the low nibble is the matrix column select and
		// bits 6 and 5 are the ALL and MUTE lamps, both active low. The lamp
		// bits are held across the whole scan - the firmware ORs them into
		// every column write - so they can be read from any one of them.
		m_leds = static_cast<uint8_t>(((~_columns >> 6) & 1) | (((~_columns >> 5) & 1) << 1));
	}

	uint8_t Sc55Mk2::panelRead()
	{
		// Rows are active low, and a column is selected by driving its P0 bit
		// low. More than one column can be low at once, in which case the read
		// is the AND of their row patterns — which is how the firmware's
		// "is anything at all pressed" fast path works.
		uint8_t data = 0xFF;
		for(uint8_t col = 0; col < 4; ++col)
		{
			if(m_panelColumns & (1u << col))
				continue;
			const uint8_t rows = static_cast<uint8_t>((m_buttons >> (col * 8)) & 0xFF);
			data &= static_cast<uint8_t>(~rows);
		}
		return data;
	}

	void Sc55Mk2::sendMidiByte(const uint8_t _byte, const MidiPort _port)
	{
		switch(_port)
		{
		case MidiInB:       m_subMcu.postMidiIn2(_byte); break;
		case ComputerPort:  m_subMcu.postComputer(_byte); break;
		case MidiInA: default: m_subMcu.postMidiIn(_byte); break;
		}
	}

	void Sc55Mk2::sendMidiBytes(const uint8_t* _data, const size_t _size, const MidiPort _port)
	{
		for(size_t i = 0; i < _size; ++i)
			sendMidiByte(_data[i], _port);
	}

	void Sc55Mk2::readMidiOut(std::vector<uint8_t>& _out)
	{
		_out.insert(_out.end(), m_midiOut.begin(), m_midiOut.end());
		m_midiOut.clear();
	}

	// =====================================================================
	// Audio
	// =====================================================================

	void Sc55Mk2::wireChip()
	{
		auto& bus = m_machine.bus();

		// The H8/532's own 32 KiB mask ROM at page-0 0x0000, and the program
		// ROM in the pages the board decodes for it — both immutable, so they
		// go into the bus image and the core fetches straight from it. The
		// board scrambles the program ROM's address lines (see romAddress),
		// so each page is copied through that mapping.
		if(m_roms.internalRom.size() >= InternalRomSize)
		{
			bus.map_rom(0x00000, InternalRomSize, h8500::BusClass::W16_S2);
			bus.load(0x00000, m_roms.internalRom.data(), InternalRomSize);
		}
		for(const uint8_t page : { 1, 2, 3, 4, 8, 9, 14, 15 })
		{
			const uint32_t base = uint32_t(page) << 16;
			bus.map_rom(base, 0x10000, h8500::BusClass::W8_S2);
			for(uint32_t off = 0; off < 0x10000; ++off)
			{
				const uint32_t a = romAddress(base + off);
				bus.mem()[base + off] = a < m_roms.programRom.size() ? m_roms.programRom[a] : 0xff;
			}
		}

		// Battery SRAM in its own pages, and everything the board decodes in
		// page 0 above the mask ROM. Page 0 stops at the on-chip RAM, which
		// the chip owns along with the register field above it.
		bus.map_device(0x08000, m_machine.config().ram_base - 0x8000, &m_boardBus, h8500::BusClass::W8_S2);
		bus.map_device(0xa0000, 0x20000, &m_boardBus, h8500::BusClass::W8_S2);
		m_machine.cpu().invalidate_all();

		// Port data registers: reads go through portRead so the board can
		// substitute the straps it drives, writes need no handler — the port
		// model keeps the latch the firmware reads back.
		m_machine.ports().set_read_hook([this](const unsigned _port, const uint8_t _value)
		{
			return portRead(_port, _value);
		});

		m_machine.adc().set_sampler([this](const unsigned _channel) -> uint16_t
		{
			return analogRead(static_cast<uint8_t>(_channel));
		});
	}

	void Sc55Mk2::observeVoiceWrite(const uint16_t _offset)
	{
		constexpr uint16_t first = 0x2dae;
		constexpr uint16_t stride = 0x12a;
		if(!m_autoVoiceReset || _offset < first || _offset >= first + stride * 28)
			return;
		const auto voice = (_offset - first) / stride;
		const auto field = (_offset - first) % stride;
		const auto bit = uint32_t{1} << voice;
		const auto base = first + stride * voice;
		const auto state = (m_sram[base] << 8) | m_sram[base + 1];
		if(field == 1)
		{
			if(state == 0x0c)
				m_releasingVoices |= bit;
			else if(state < 0x0c)
			{
				m_releasingVoices &= ~bit;
				m_pendingVoiceResets &= ~bit;
			}
		}
		// H8 routine 0x3095 ends the release after GP completion. The
		// software accumulator may retain its final fractional remainder.
		if(field == 1 && (m_releasingVoices & bit) && state == 0x16)
			m_pendingVoiceResets |= bit;
	}

	Sc55Mk2::SampleFrame Sc55Mk2::renderSample()
	{
		if(!m_valid)
			return { 0, 0 };

		++m_samplesRendered;

		// Advance the H8 by one audio frame's worth of phi cycles. Integer
		// Bresenham so the ratio stays exact regardless of the sample rate.
		m_cycleTarget += CpuClockHz / SampleRate;
		m_cycleFrac   += static_cast<uint32_t>(CpuClockHz % SampleRate);
		if(m_cycleFrac >= SampleRate)
		{
			m_cycleFrac -= SampleRate;
			++m_cycleTarget;
		}

		if(m_cycleTarget > cycles())
			m_machine.run(m_cycleTarget - cycles());

		// The sub-MCU runs on its own time base, derived from the sample
		// counter (see SubMcuCyclesPerSample). It is advanced AFTER the CPU
		// slice so a byte it delivers this sample is visible to the firmware
		// on the next one, which is the direction the real handshake runs.
		m_subMcu.update(subMcuCycles(m_samplesRendered));

		if((m_samplesRendered & 255u) == 0 && m_pendingVoiceResets)
		{
			for(unsigned voice = 0; voice < 28; ++voice)
				if(m_pendingVoiceResets & (uint32_t{1} << voice))
					m_gp.retireVoice(voice);
			m_pendingVoiceResets = 0;
		}
		return m_gp.renderFrame();
	}
}
