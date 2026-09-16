
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

#include "88lib/boards/sc55Board.h"

#include <cstring>

#include "common/romDescramble.h"

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

	Sc55Board::Sc55Board(Sc55RomSet _roms, const bool _factoryReset)
		: m_roms(std::move(_roms))
		, m_profile(m_roms.profile())
		, m_gp(gpLib::GpConfig{m_profile.generation == Sc55Generation::First ? Mk1GpClockHz : Mk2GpClockHz,
		                       m_profile.generation == Sc55Generation::First})
	{
		m_valid = m_roms.isValid();

		// The original has three 1 MiB mask ROMs; the mkII has a 2 MiB and a
		// 1 MiB part. Profile data maps each physical ROM to its GP chip select.
		for(uint8_t slot = 0; slot < m_roms.waveRom.size(); ++slot)
		{
			if(m_roms.waveRom[slot].empty())
				continue;
			descrambleWaveRom(m_roms.waveRom[slot]);
			m_gp.setWaveRom(m_profile.waveBanks[slot], m_roms.waveRom[slot]);
		}

		// The GP's voice-end line goes straight to IRQ0; only the gate array's
		// own sources are multiplexed onto IRQ1.
		m_gp.setIrqCallback([this](const bool _level) { requestIrq(IrqGp, _level); });

		if(!usesSubMcu())
		{
			// First-generation and embedded mkII-derived boards wire DIN MIDI
			// directly to the H8/532 SCI.
			m_machine.sci(0).set_tx_sink([this](const uint8_t _b, uint64_t)
			{
				m_midiOut.push_back(_b);
			});
		}
		else
		{
			Sc55SubMcu::Hooks hooks;
			// The sub-MCU is gate-array line 5. It pulses the line 1 then 0; the
			// gate array latches the rising edge, so both halves are forwarded.
			hooks.hostIrq  = [this](const int _level) { setGateArrayInt(g_gaLineSubMcu, _level != 0); };
			hooks.midiOut  = [this](const uint8_t _b) { m_midiOut.push_back(_b); };
			// The panel matrix is board wiring: the firmware drives the columns
			// through the sub-MCU's P0 and reads the rows back on P1.
			hooks.writeP0  = [this](const uint8_t _v) { panelWrite(_v); };
			hooks.readP1   = [this]() { return panelRead(); };
			m_subMcu.setHooks(hooks);
		}

		wireChip();
		powerCycle();
		// Every board with a panel takes the procedure; the headless ones have no switches to drive.
		if(_factoryReset && hasPanel())
			runFactoryReset();
	}

	void Sc55Board::powerCycle()
	{
		// Preserve only battery-backed SRAM, the rear-panel switch and the board wiring. Everything
		// else below is volatile state that a physical power cycle clears.
		m_gp.reset();
		m_lcd.reset();
		m_subMcu.reset();
		m_machine.reset();

		// The chip's state counter is monotonic across a reset, so the pacing target rebases onto it
		// rather than onto zero. The sub-MCU's clock is the sample counter, which restarts with it.
		m_samplesRendered = 0;
		m_cycleTarget = m_machine.now();
		m_cycleFrac = 0;

		m_buttons = 0;
		m_panelColumns = 0xFF;
		m_leds = 0;
		m_midiOut.clear();

		m_gaInt.fill(false);
		m_gaIntTrigger = 0;
		// The first-generation decode never reaches the enable register, so its lines are always open.
		m_gaIrqEnable = isFirstGen() && !usesSubMcu() ? 0xFF : 0;
		m_ioSd = 0;
		m_lcdEnabled = hasDisplay();
		m_lcdIrqAt = 0;
		requestIrq(IrqGp, false);
		requestIrq(IrqGateArray, false);
	}

	void Sc55Board::runFactoryReset()
	{
		if(!m_valid) return;
		// The SC-55 and SC-55mkII owner's manuals use INSTRUMENT left+right at power-on, then ALL,
		// and the SC-155 and SC-155mkII firmware answer the same keys with "Init All, Sure?". Run it
		// before exposing the board so its firmware initializes retained settings: from blank
		// battery RAM the SC-155s otherwise come up with nonsense part settings.
		const auto run = [this](uint32_t samples)
		{
			while(samples--) renderSample();
		};
		setButton(Button::InstL, true);
		setButton(Button::InstR, true);
		run(8 * sampleRate());
		setButtons(0);
		run(sampleRate() / 4);
		setButton(Button::InstAll, true);
		run(sampleRate() / 10);
		setButtons(0);
		run(2 * sampleRate());
		// A real unit then boots from the initialized settings, so complete that power cycle
		// instead of carrying on with the pre-reset volatile part state.
		powerCycle();
	}

	// =====================================================================
	// Address decode
	// =====================================================================

	void Sc55Board::descrambleWaveRom(std::vector<uint8_t>& _rom)
	{
		// Bits above 19 pass through, so the permutation repeats once per 1 MiB
		// chip rather than spanning a multi-chip image.
		std::vector<uint8_t> out(_rom.size());
		rLib::rom::Pcm8::descramble(_rom.data(), _rom.size(), out.data(), out.size());
		_rom.swap(out);
	}

	uint32_t Sc55Board::romAddress(const uint32_t _addr)
	{
		// The board wires the ROM's high address line off CPU address bit 19
		// rather than continuing the linear sequence, so pages 8-9 and 14-15
		// reach the ROM's upper half while pages 1-4 reach the lower one.
		uint32_t a = _addr & 0x3FFFF;
		if(_addr & 0x80000)
			a |= 0x40000;
		return a;
	}

	uint8_t Sc55Board::programRomByte(const uint32_t _addr) const
	{
		if(m_roms.programRom.empty())
			return 0xFF;
		// The 256 KiB mkI image is mirrored through the same board decode that
		// addresses the mkII's 512 KiB image.
		return m_roms.programRom[romAddress(_addr) & (m_roms.programRom.size() - 1)];
	}

	uint8_t Sc55Board::extRead8(const uint32_t _addr)
	{
		const uint8_t v = extRead8Raw(_addr);
		return v;
	}

	uint8_t Sc55Board::extRead8Raw(const uint32_t _addr)
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
			if(off <= (isMk1() ? uint16_t(0xE03F) : g_gpEnd))
				return m_gp.read8(off & 0x3F);
			if(isMk1())
			{
				if(off == 0xF106)
				{
					const uint8_t value = m_gaIntTrigger;
					m_gaIntTrigger = 0;
					requestIrq(IrqGateArray, false);
					return value;
				}
				if(off >= 0xF000 && off < 0xF100)
					return mk1PanelRead(off);
				return 0xFF;
			}
			if(off >= g_gaBase && off <= g_gaEnd)
				return gateArrayRead(static_cast<uint16_t>(off - g_gaBase));
			if(usesSubMcu() && off >= g_subMcuBase && off <= g_subMcuEnd)
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
				return programRomByte(_addr);
			}
		case 5:
			if(isMk1())
				return m_sram[off & (SramSize - 1)];
			return 0xFF;
		case 10: case 11:
			return isMk1() ? 0xFF : m_sram[off & (SramSize - 1)];
		default:
			return 0xFF;
		}
	}

	void Sc55Board::extWrite8(const uint32_t _addr, const uint8_t _val)
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
				return;
			}
			if(off <= (isMk1() ? uint16_t(0xE03F) : g_gpEnd))
			{
				m_gp.write8(off & 0x3F, _val);
				return;
			}
			if(isMk1())
			{
				if(off == 0xF104) mk1LcdWrite(true, _val);
				else if(off == 0xF105) mk1LcdWrite(false, _val);
				else if(off == 0xF107) m_ioSd = _val;
				else if(off >= 0xF000 && off < 0xF100)
				{
					m_ioSd = static_cast<uint8_t>(off);
					m_lcdEnabled = (m_ioSd & 8) != 0;
				}
				return;
			}
			if(off >= g_gaBase && off <= g_gaEnd)
			{
				gateArrayWrite(static_cast<uint16_t>(off - g_gaBase), _val);
				return;
			}
			if(usesSubMcu() && off >= g_subMcuBase && off <= g_subMcuEnd)
			{
				m_subMcu.hostWrite(static_cast<uint8_t>(off & 0xFF), _val);
				return;
			}
			return;
		}

		if((isMk1() && page == 5) || (!isMk1() && (page == 10 || page == 11)))
			m_sram[off & (SramSize - 1)] = _val;
		// Everything else is ROM or unmapped.
	}

	// =====================================================================
	// Gate array
	// =====================================================================

	uint8_t Sc55Board::gateArrayRead(const uint16_t _addr)
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

	void Sc55Board::gateArrayWrite(const uint16_t _addr, const uint8_t _val)
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

	void Sc55Board::setGateArrayInt(const uint8_t _line, const bool _level)
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

	uint8_t Sc55Board::portRead(const unsigned _port, const uint8_t _value)
	{
		if(_port == 9)
		{
			const uint8_t cfg = m_profile.p9Strap;
			const uint8_t dir = m_machine.ports().ddr(9);
			return static_cast<uint8_t>((cfg & static_cast<uint8_t>(~dir)) | (_value & dir));
		}
		return _value;
	}

	uint16_t Sc55Board::analogRead(const uint8_t _channel)
	{
		if(m_roms.model == DeviceModel::Cm300 || m_roms.model == DeviceModel::Scc1a)
			return 0;
		if(isMk1())
		{
			// The SC-155 selects its (currently neutral) part sliders through
			// P9DR. This mirrors the reference model's hardware mux even though
			// slider controls are not exposed yet.
			if(m_roms.model == DeviceModel::Sc155)
			{
				const auto p9 = m_machine.ports().dr(9);
				if((p9 & 1) != 0 || (_channel == 7 && (p9 & 2) != 0))
					return 0;
			}
			return _channel == 7 ? AnalogBattery : 0;
		}

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

	void Sc55Board::setButton(const Button _button, const bool _pressed)
	{
		setButton(static_cast<uint32_t>(_button), _pressed);
	}

	void Sc55Board::setButton(const uint32_t _index, const bool _pressed)
	{
		if(!hasPanel() || _index >= g_buttonCount)
			return;
		if(_pressed) m_buttons |=  (1u << _index);
		else         m_buttons &= ~(1u << _index);
	}

	void Sc55Board::panelWrite(const uint8_t _columns)
	{
		if(!hasPanel())
			return;
		m_panelColumns = _columns;

		// P0 does double duty: the low nibble is the matrix column select and
		// bits 6 and 5 are the ALL and MUTE lamps, both active low. The lamp
		// bits are held across the whole scan - the firmware ORs them into
		// every column write - so they can be read from any one of them.
		m_leds = static_cast<uint8_t>(((~_columns >> 6) & 1) | (((~_columns >> 5) & 1) << 1));
	}

	uint8_t Sc55Board::panelRead()
	{
		if(!hasPanel())
			return 0xFF;
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

	uint8_t Sc55Board::mk1PanelRead(const uint16_t _addr)
	{
		m_ioSd = static_cast<uint8_t>(_addr);
		if(!hasPanel())
			return 0xFF;
		m_lcdEnabled = (m_ioSd & 8) != 0;
		m_panelColumns = m_ioSd;
		return panelRead();
	}

	void Sc55Board::mk1LcdWrite(const bool _data, const uint8_t _value)
	{
		if(!hasDisplay())
			return;
		m_lcd.write(_data, _value);
		m_lcdIrqAt = cycles() + 500;
	}

	void Sc55Board::sendMidiByte(const uint8_t _byte, const MidiPort _port)
	{
		if(!usesSubMcu())
		{
			if(_port == MidiInA)
				m_machine.sci(0).receive_byte(_byte);
			return;
		}
		switch(_port)
		{
		case MidiInB:       m_subMcu.postMidiIn2(_byte); break;
		case ComputerPort:  m_subMcu.postComputer(_byte); break;
		case MidiInA: default: m_subMcu.postMidiIn(_byte); break;
		}
	}

	void Sc55Board::sendMidiBytes(const uint8_t* _data, const size_t _size, const MidiPort _port)
	{
		for(size_t i = 0; i < _size; ++i)
			sendMidiByte(_data[i], _port);
	}

	void Sc55Board::readMidiOut(std::vector<uint8_t>& _out)
	{
		_out.insert(_out.end(), m_midiOut.begin(), m_midiOut.end());
		m_midiOut.clear();
	}

	// =====================================================================
	// Audio
	// =====================================================================

	void Sc55Board::wireChip()
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
				bus.mem()[base + off] = programRomByte(base + off);
		}

		// Battery SRAM in its own pages, and everything the board decodes in
		// page 0 above the mask ROM. Page 0 stops at the on-chip RAM, which
		// the chip owns along with the register field above it.
		bus.map_device(0x08000, m_machine.config().ram_base - 0x8000, &m_boardBus, h8500::BusClass::W8_S2);
		if(isFirstGen())
			bus.map_device(0x50000, 0x10000, &m_boardBus, h8500::BusClass::W8_S2);
		else
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

	Sc55Board::SampleFrame Sc55Board::renderSample()
	{
		if(!m_valid)
			return { 0, 0 };

		++m_samplesRendered;

		// Advance the H8 by one audio frame's worth of phi cycles. Integer
		// Bresenham so the ratio stays exact regardless of the sample rate.
		const auto rate = sampleRate();
		const auto cpuRate = cpuClockHz();
		m_cycleTarget += cpuRate / rate;
		m_cycleFrac   += static_cast<uint32_t>(cpuRate % rate);
		if(m_cycleFrac >= rate)
		{
			m_cycleFrac -= rate;
			++m_cycleTarget;
		}

		if(m_cycleTarget > cycles())
			m_machine.run(m_cycleTarget - cycles());

		// The sub-MCU runs on its own time base, derived from the sample
		// counter (see SubMcuCyclesPerSample). It is advanced AFTER the CPU
		// slice so a byte it delivers this sample is visible to the firmware
		// on the next one, which is the direction the real handshake runs.
		if(isMk1())
		{
			if(m_lcdIrqAt != 0 && cycles() >= m_lcdIrqAt)
			{
				m_lcdIrqAt = 0;
				setGateArrayInt(1, false);
				setGateArrayInt(1, true);
			}
		}
		else if(usesSubMcu())
			m_subMcu.update(subMcuCycles(m_samplesRendered));

		return m_gp.renderFrame();
	}
}
