#include "88lib/boards/mv30.h"
#include "88lib/boards/cm32p.h"

#include <algorithm>

#include "common/romDescramble.h"

namespace emu88Lib
{
	namespace
	{
		using hg62e33b08fLib::HG62E33B08F;

		constexpr uint16_t g_ioBank = 0x474;
		constexpr uint16_t g_ioBankEnd = HG62E33B08F::BootBank;
		constexpr uint32_t g_ramBanks = 0x400;

		// Gate array interrupt sources, numbered as the boot ROM's entry stubs.
		constexpr unsigned g_irqPanel = 1;
		constexpr unsigned g_irqFdc = 2;

		// Where each wave ROM sits in the LP's 4 MiB space. The two holes, 0x280000 and
		// 0x380000, are the card slots.
		constexpr std::array<uint32_t, 6> g_waveBase{0x000000, 0x100000, 0x200000, 0x300000, 0x080000, 0x180000};

		constexpr uint32_t g_panelTickSamples = Mv30::SampleRate / 1000;
		constexpr uint32_t g_osModeAddress = 0xa1 * 0x400 + (0x61b5 - 0x4000);

		// The current song's part settings, the arrays the Play screen shows: a byte per part, nine entries apart,
		// the receive channels last; then the 128 timbres, 56 bytes each starting with the 12-character name. The
		// same physical addresses on OS 1.08 and 1.091, in data bank 0B1, found by changing each setting by MIDI and
		// watching the RAM. The front panel's own edits land there as well.
		constexpr uint32_t g_partTimbre = 0x2defd;
		constexpr uint32_t g_partOutput = 0x2df06;
		constexpr uint32_t g_partLevel = 0x2df0f;
		constexpr uint32_t g_partPan = 0x2df18;
		constexpr uint32_t g_partVoiceReserve = 0x2df21;
		constexpr uint32_t g_partChannel = 0x2df2a;
		constexpr uint32_t g_timbres = 0x2df32;
		constexpr uint32_t g_timbreSize = 0x38;

		// The voice buses between the sound chips carry bits 1-17 of the sender's sample
		// and take the sign from a higher bit: the LP's bit 21 into the TVF, the TVF's
		// bit 23 into the RCC.
		int32_t busTap(const int32_t _sample, const unsigned _signBit)
		{
			const auto bus = static_cast<uint32_t>(_sample);
			const uint32_t word = ((bus >> 1) & 0x1ffff) | (((bus >> _signBit) & 1) << 17);
			return static_cast<int32_t>(word) - ((word & 0x20000) ? 0x40000 : 0);
		}
	}

	std::vector<uint8_t> Mv30::decodeWaves(const Mv30RomSet::WaveRoms& _waves)
	{
		std::vector<uint8_t> decoded(0x400000, 0xff);
		for (size_t i = 0; i < _waves.size(); ++i)
			rLib::rom::Wave19::descramble(_waves[i].data(), Mv30RomSet::WaveSize, decoded.data() + g_waveBase[i], Mv30RomSet::WaveSize);
		return decoded;
	}

	Mv30::Mv30(const Mv30RomSet& _roms, const std::vector<uint8_t>& _card)
		: m_boot(_roms.isValid() ? _roms.boot : std::vector<uint8_t>{})
		, m_waves(_roms.isValid() ? decodeWaves(_roms.waves) : std::vector<uint8_t>{})
		, m_disk(_roms.isValid() ? _roms.systemDisk : std::vector<uint8_t>{}, hwLib::FloppyImage::SectorsDD)
		, m_lcd(240, 64, _roms.cgRom.size() == Mv30RomSet::CgRomSize ? _roms.cgRom : std::vector<uint8_t>{})
	{
		m_valid = _roms.isValid() && m_disk.isValid();
		if (!m_valid)
			return;
		if (const auto card = Cm32p::decodeCard(_card); !card.empty())
		{
			std::copy(card.begin(), card.end(), m_waves.begin() + CardABase);
			m_cardInserted = true;
		}

		// Everything above the register file is the board's: the gate array's own page at
		// 0x0100 and the four banked windows.
		m_machine.bus().map_device(0x0100, 0xff00, &m_boardDevice);

		m_gate.setBankCallback([this](const unsigned _window, const uint16_t _bank)
		{
			m_machine.cpu().select_code_bank(_window * HG62E33B08F::WindowSize, HG62E33B08F::WindowSize, _bank);
		});
		m_gate.setIrqCallback([this](const bool _level)
		{
			m_machine.periph().set_external_interrupt_input(1, _level);
			updatePort2();
		});

		auto& periph = m_machine.periph();
		periph.set_port_output_hook(1, [this](const uint8_t _v) { onPort1(_v); });
		periph.set_port_output_hook(2, [this](const uint8_t _v) { onPort2(_v); });
		periph.set_hso_hook([this](const uint8_t _v) { onHso(_v); });
		periph.set_serial_tx_byte_hook([this](const uint8_t _v) { m_midiOut.write(_v); });

		m_lp.setInterruptCallback([this](const bool _level)
		{
			m_lpIrq = _level;
			m_machine.periph().set_external_interrupt_input(0, _level);
			updatePort0();
		});

		m_fdc.setDisk(&m_disk);
		m_fdc.setIrqCallback([this](const bool _level) { m_gate.setSource(g_irqFdc, _level); });
		m_fdc.setDrqCallback([this](const bool _level) { onFdcDrq(_level); });
		m_fdc.setStepCallback([this] { m_diskChanged = false; updatePort2(); });

		m_faders.fill(FaderMax);

		reset();
	}

	void Mv30::reset(const bool _bootOffline)
	{
		if (!m_valid)
			return;

		std::fill(m_ram.begin(), m_ram.end(), 0);
		m_codeLines.reset();
		m_machine.cpu().invalidate_all_banks();
		m_gate.reset();

		m_lp.reset();
		m_tvf.reset();
		m_rcc.reset();
		m_rccSerialDelay.fill(0);
		std::fill(std::begin(m_lpRegisters), std::end(m_lpRegisters), 0);

		m_fdc.reset();
		m_fdc.setDataRate(250000);
		m_fdc.setReady(true);
		m_diskChanged = false;
		m_tcLine = false;

		m_lcd.reset();

		m_panel.fill(0);
		m_panelIndex = 0;
		m_keysPrevious.fill(0);
		m_keyFifo.fill(0);
		m_keyHead = m_keyCount = 0;
		m_panelIrq = false;
		m_panelTickPhase = 0;
		m_osReady = false;

		m_faderSelect = 0;
		m_dialPending = 0;
		m_encoderEvent = m_encoderNegative = m_encoderClear = false;
		m_lpIrq = false;
		m_outputMute = true;
		m_hso = 0;

		m_midiOut = synthLib::MidiBufferParser(synthLib::MidiEventSource::Device);
		m_midiIn = std::make_unique<synthLib::MidiRateLimiter>([this](const uint8_t _value) { m_machine.periph().receive_serial(_value); });
		m_midiIn->setSamplerate(SampleRate);
		m_midiIn->setRateLimit(3125);
		m_midiIn->setPreserveEventOrder(true);

		m_machine.reset();

		auto& periph = m_machine.periph();
		periph.set_port_input(1, 0xff);
		// Footswitch released (low through Q3), card sense low with a card in (B on HSI1, A on
		// HSI2), HSI3 high.
		periph.set_hsi_input(0, false);
		periph.set_hsi_input(1, true);
		periph.set_hsi_input(2, !m_cardInserted);
		periph.set_hsi_input(3, true);
		periph.set_analog_input(0, m_faders[0]);
		updatePort0();
		updatePort2();

		m_samplesRendered = 0;
		m_cycleTarget = m_machine.now();
		m_cycleFraction = 0;

		m_booted = _bootOffline && bootOffline();
	}

	bool Mv30::osRunning() const
	{
		// The boot ROM runs from bank 0x780; the OS from RAM.
		return m_gate.bank(m_machine.cpu().regs().pc >> 14) < g_ramBanks;
	}

	bool Mv30::bootOffline(const float _maxSeconds)
	{
		if (!m_valid)
			return false;
		const auto limit = static_cast<uint64_t>(_maxSeconds * SampleRate);
		for (uint64_t i = 0; i < limit && !osReady(); ++i)
			renderSample();
		return osReady();
	}

	bool Mv30::osReady() const
	{
		if (!m_osReady || !m_machine.periph().serial_receive_enabled())
			return false;
		// The OS's mode (OS 1.08 and 1.091 keep it at 0x61B5 in data bank 0A1): 0 while it starts, 0xFF while it
		// switches screens. A quarter of a second covers an OS that keeps it elsewhere.
		const uint8_t mode = m_ram[g_osModeAddress];
		return (mode != 0 && mode != 0xff) || m_samplesRendered - m_osReadySample >= SampleRate / 4;
	}

	void Mv30::readParts(Mv30Parts& _parts) const
	{
		_parts.valid = m_valid && osReady();
		if (!_parts.valid)
		{
			_parts = Mv30Parts{};
			return;
		}
		for (uint8_t p = 0; p < Mv30Parts::PartCount; ++p)
		{
			auto& part = _parts.parts[p];
			part.timbre = m_ram[g_partTimbre + p];
			part.output = m_ram[g_partOutput + p];
			part.level = m_ram[g_partLevel + p];
			part.pan = static_cast<int8_t>(m_ram[g_partPan + p]);
			part.voiceReserve = m_ram[g_partVoiceReserve + p];
			part.channel = m_ram[g_partChannel + p];
		}
		for (uint32_t t = 0; t < Mv30Parts::TimbreCount; ++t)
			std::copy_n(m_ram.begin() + g_timbres + t * g_timbreSize, Mv30Parts::NameLength,
				_parts.timbreNames[t].begin());
	}

	// =====================================================================
	// CPU bus
	// =====================================================================

	uint8_t Mv30::read8(const uint16_t _address)
	{
		const bool fetch = m_machine.cpu().fetching();
		if (!fetch && _address >= HG62E33B08F::RegisterBase && _address < HG62E33B08F::RegisterBase + HG62E33B08F::RegisterCount)
			return m_gate.read(static_cast<uint8_t>(_address - HG62E33B08F::RegisterBase));
		return readMemory(_address, fetch);
	}

	void Mv30::write8(const uint16_t _address, const uint8_t _value)
	{
		if (_address >= HG62E33B08F::RegisterBase && _address < HG62E33B08F::RegisterBase + HG62E33B08F::RegisterCount)
		{
			m_gate.write(static_cast<uint8_t>(_address - HG62E33B08F::RegisterBase), _value);
			return;
		}
		writeMemory(_address, _value);
	}

	uint16_t Mv30::read16(const uint16_t _address)
	{
		const bool fetch = m_machine.cpu().fetching();
		// A word inside one RAM window is the common case (every instruction fetch is one).
		if ((_address & 0x3fff) != 0x3fff && _address >= HG62E33B08F::RegisterBase + HG62E33B08F::RegisterCount)
		{
			const uint32_t physical = m_gate.physical(_address, fetch);
			if (physical < RamSize - 1)
			{
				if (fetch)
					m_codeLines.set(physical >> 7);
				const auto word = static_cast<uint16_t>(m_ram[physical] | (m_ram[physical + 1] << 8));
				return !fetch && _address == HG62E33B08F::Extint1Vector ? m_gate.vector(word) : word;
			}
		}
		const auto word = static_cast<uint16_t>(read8(_address) | (read8(static_cast<uint16_t>(_address + 1)) << 8));
		return !fetch && _address == HG62E33B08F::Extint1Vector ? m_gate.vector(word) : word;
	}

	uint8_t Mv30::readMemory(const uint16_t _address, const bool _fetch)
	{
		const uint16_t bank = m_gate.bank((_address >> 14) + (_fetch ? 0 : 4));
		const uint32_t offset = _address & (HG62E33B08F::WindowSize - 1);
		if (bank < g_ramBanks)
		{
			const uint32_t physical = bank * HG62E33B08F::BankUnit + offset;
			if (physical >= RamSize)
				return 0xff;
			if (_fetch)
				m_codeLines.set(physical >> 7);
			return m_ram[physical];
		}
		if (bank < g_ioBankEnd)
			return _fetch ? 0xff : peripheralRead(static_cast<uint16_t>((bank - g_ioBank) * HG62E33B08F::BankUnit + offset));
		return m_boot[(bank * HG62E33B08F::BankUnit + offset) & (Mv30RomSet::BootSize - 1)];
	}

	void Mv30::writeMemory(const uint16_t _address, const uint8_t _value)
	{
		const uint16_t bank = m_gate.bank((_address >> 14) + 4);
		const uint32_t offset = _address & (HG62E33B08F::WindowSize - 1);
		if (bank < g_ramBanks)
		{
			const uint32_t physical = bank * HG62E33B08F::BankUnit + offset;
			if (physical < RamSize)
			{
				m_ram[physical] = _value;
				noteRamWrite(physical);
			}
		}
		else if (bank < g_ioBankEnd)
			peripheralWrite(static_cast<uint16_t>((bank - g_ioBank) * HG62E33B08F::BankUnit + offset), _value);
	}

	void Mv30::noteRamWrite(const uint32_t _physical)
	{
		if (!m_codeLines.test(_physical >> 7))
			return;
		// Instructions were decoded from this line, through whichever window mapped it at
		// the time: drop everything rather than work out which.
		m_machine.cpu().invalidate_all_banks();
		m_codeLines.reset();
	}

	// =====================================================================
	// Peripheral page
	// =====================================================================

	uint8_t Mv30::peripheralRead(const uint16_t _offset)
	{
		if (_offset < 0x800)
			return m_rcc.read(_offset);
		if (_offset == 0x801)
			return panelRead(m_panelIndex);
		if (_offset == 0x1000)
			return m_fdc.readStatus();
		if (_offset == 0x1001)
			return m_fdc.readFifo();
		if (_offset >= 0x1800 && _offset < 0x1900)
			return lpRead(static_cast<uint8_t>(_offset - 0x1800));
		if (_offset == 0x2800 || _offset == 0x2802)
			return m_lcd.read(static_cast<uint16_t>((_offset - 0x2800) >> 1));
		if (_offset >= 0x3000 && _offset < 0x3100)
			return m_tvf.read(static_cast<uint16_t>(_offset - 0x3000));
		return 0xff;
	}

	void Mv30::peripheralWrite(const uint16_t _offset, const uint8_t _value)
	{
		if (_offset < 0x800)
			m_rcc.write(_offset, _value);
		else if (_offset == 0x800)
			m_panelIndex = _value & 0x7f;
		else if (_offset == 0x801)
			panelWrite(m_panelIndex, _value);
		else if (_offset == 0x1000)
			m_fdc.writeAuxCommand(_value);
		else if (_offset == 0x1001)
			m_fdc.writeFifo(_value);
		else if (_offset >= 0x1800 && _offset < 0x1900)
		{
			const auto offset = static_cast<uint8_t>(_offset - 0x1800);
			m_lpRegisters[offset] = _value;
			if (offset < 0x20)
				m_lp.write(offset, _value);
		}
		else if (_offset == 0x2800 || _offset == 0x2802)
			m_lcd.write(static_cast<uint16_t>((_offset - 0x2800) >> 1), _value);
		else if (_offset >= 0x3000 && _offset < 0x3100)
			m_tvf.write(static_cast<uint16_t>(_offset - 0x3000), _value);
	}

	uint8_t Mv30::lpRead(const uint8_t _offset)
	{
		// Address bit 5 swaps the LP's own wave-data port (offset 1) with the board's: at 01
		// the board reads the wave ROMs through the address the CPU staged in registers
		// 09-0B, with all of register 03 as the bank; at 21 the LP answers. The rest of 20-FF
		// reads back what was written.
		if (_offset == 0x01)
		{
			const uint32_t staged = m_lpRegisters[9] | (uint32_t(m_lpRegisters[10]) << 8) | (uint32_t(m_lpRegisters[11]) << 16);
			const uint32_t address = (((staged >> 6) + 2) & 0x3ffff) | (uint32_t(m_lpRegisters[3]) << 16);
			return address < m_waves.size() ? m_waves[address] : 0xff;
		}
		if (_offset == 0x21)
			return m_lp.read(1);
		if (_offset < 0x20)
			return m_lp.read(_offset);
		return m_lpRegisters[_offset < 0x40 ? _offset - 0x20 : _offset];
	}

	// =====================================================================
	// TC23SC060 panel gate array
	// =====================================================================
	//
	// A register index at +0800 and a data port at +0801. Registers 00-07 read the switch
	// matrix rows (pressed = 1), 08-17 are the LED words, two per scan row. Register 70 bit 7
	// turns on the autonomous scan, which queues each newly pressed switch as row * 8 +
	// column and raises the panel interrupt; register 78 pops the queue (bit 7 = an event).
	// The scan period and queue depth are not known; contact debounce is not modelled.

	uint8_t Mv30::panelRead(const uint8_t _reg)
	{
		if (_reg < 8)
			return m_keys[_reg];
		if (_reg == 0x78)
		{
			if (!m_keyCount)
				return 0;
			const auto result = static_cast<uint8_t>(0x80 | m_keyFifo[m_keyHead]);
			// Taking an event acknowledges the interrupt; another queued one raises it again.
			m_panelIrq = false;
			updatePanelIrq();
			m_keyHead = (m_keyHead + 1) & 15;
			--m_keyCount;
			m_panelIrq = m_keyCount != 0;
			updatePanelIrq();
			return result;
		}
		return m_panel[_reg & 0x7f];
	}

	void Mv30::panelWrite(const uint8_t _reg, const uint8_t _value)
	{
		m_panel[_reg & 0x7f] = _value;
		if (_reg != 0x70)
			return;
		updatePanelIrq();
		// The boot ROM turns the scan on at once; the OS turns it off while it starts and
		// back on when it is ready for the panel.
		if ((_value & 0x80) && osRunning() && !m_osReady)
		{
			m_osReady = true;
			m_osReadySample = m_samplesRendered;
		}
	}

	void Mv30::updatePanelIrq()
	{
		m_gate.setSource(g_irqPanel, m_panelIrq && (m_panel[0x70] & 0x80));
	}

	void Mv30::panelTick()
	{
		if (m_panel[0x70] & 0x80)
		{
			for (unsigned row = 0; row < 8; ++row)
			{
				const uint8_t pressed = m_keys[row] & ~m_keysPrevious[row];
				m_keysPrevious[row] = m_keys[row];
				for (unsigned column = 0; column < 8; ++column)
				{
					if (!(pressed & (1u << column)) || m_keyCount >= m_keyFifo.size())
						continue;
					m_keyFifo[(m_keyHead + m_keyCount) & 15] = static_cast<uint8_t>(row * 8 + column);
					++m_keyCount;
				}
			}
			m_panelIrq = m_keyCount != 0;
			updatePanelIrq();
		}

		// IC14's two flip-flops hold one encoder event until the HSO2 clear pulse; further
		// detents wait for it.
		if (!m_encoderEvent && !m_encoderClear && m_dialPending)
		{
			m_encoderEvent = true;
			m_encoderNegative = m_dialPending < 0;
			m_dialPending += m_encoderNegative ? 1 : -1;
			updatePort0();
		}
	}

	void Mv30::setButton(const Mv30Button _button, const bool _pressed)
	{
		const auto index = static_cast<uint8_t>(_button);
		auto& row = m_keys[(index >> 3) & 7];
		const auto bit = static_cast<uint8_t>(1u << (index & 7));
		row = _pressed ? static_cast<uint8_t>(row | bit) : static_cast<uint8_t>(row & ~bit);
	}

	void Mv30::setButtons(const uint64_t _buttons)
	{
		for (size_t row = 0; row < m_keys.size(); ++row)
			m_keys[row] = static_cast<uint8_t>(_buttons >> (row * 8));
	}

	bool Mv30::isButtonPressed(const Mv30Button _button) const
	{
		const auto index = static_cast<uint8_t>(_button);
		return (m_keys[(index >> 3) & 7] >> (index & 7)) & 1;
	}

	void Mv30::turnDial(const int _detents)
	{
		m_dialPending = std::clamp(m_dialPending + _detents, -32768, 32767);
	}

	void Mv30::setFader(const unsigned _index, const uint16_t _value)
	{
		if (_index >= m_faders.size())
			return;
		m_faders[_index] = std::min<uint16_t>(_value, FaderMax);
		if (_index == m_faderSelect)
		{
			m_machine.periph().set_analog_input(0, m_faders[_index]);
			updatePort0();
		}
	}

	void Mv30::setFootswitch(const bool _pressed)
	{
		m_machine.periph().set_hsi_input(0, _pressed);
	}

	// =====================================================================
	// CPU pins
	// =====================================================================

	void Mv30::updatePort0()
	{
		// ACH0 also reads digitally, as the selected fader against mid-scale. ACH1-4 are
		// grounded.
		uint8_t value = m_faders[m_faderSelect] >= 0x200 ? 0x01 : 0x00;
		if (m_encoderNegative) value |= 0x20;
		if (m_encoderEvent) value |= 0x40;
		if (m_lpIrq) value |= 0x80;
		m_machine.periph().set_port_input(0, value);
	}

	void Mv30::updatePort2()
	{
		// P2.2 reads the gate array's INT, P2.3 the drive's disk-change line.
		uint8_t value = 0xf3;
		if (m_gate.irq()) value |= 0x04;
		if (m_diskChanged) value |= 0x08;
		m_machine.periph().set_port_input(2, value);
	}

	void Mv30::onPort1(const uint8_t _value)
	{
		// P1.0-2 select a fader through IC18 (BU4051), P1.3 mutes the line outputs.
		m_faderSelect = _value & 7;
		m_outputMute = (_value & 0x08) != 0;
		m_machine.periph().set_analog_input(0, m_faders[m_faderSelect]);
		updatePort0();
	}

	void Mv30::onPort2(const uint8_t _value)
	{
		// P2.5 is the firmware's own terminal count to the FDC, beside the gate array's.
		// The firmware asserts it to end a partial sector.
		const bool tc = (_value & 0x20) != 0;
		if (tc)
			m_gate.dmaStop();
		if (tc != m_tcLine)
		{
			m_tcLine = tc;
			m_fdc.setTerminalCount(tc);
		}
	}

	void Mv30::onHso(const uint8_t _value)
	{
		// HSO2 low clears the encoder latch. HSO0/1/3 drive the metronome, not modelled.
		m_hso = _value;
		m_encoderClear = (_value & 0x04) == 0;
		if (m_encoderClear && m_encoderEvent)
		{
			m_encoderEvent = false;
			updatePort0();
		}
	}

	void Mv30::onFdcDrq(const bool _level)
	{
		if (!_level || !m_gate.dmaActive() || !m_gate.dmaToMemory())
			return;
		const uint32_t physical = m_gate.dmaAddress() & (RamSize - 1);
		m_ram[physical] = m_fdc.dmaRead();
		noteRamWrite(physical);
		if (m_gate.dmaStep())
		{
			// Terminal count, pulsed the way a DMA controller strobes EOP.
			m_fdc.setTerminalCount(true);
			m_fdc.setTerminalCount(m_tcLine);
		}
	}

	// =====================================================================
	// Time and audio
	// =====================================================================

	void Mv30::runCpuForSample()
	{
		m_cycleTarget += CpuStateRate / SampleRate;
		m_cycleFraction += CpuStateRate % SampleRate;
		if (m_cycleFraction >= SampleRate)
		{
			m_cycleFraction -= SampleRate;
			++m_cycleTarget;
		}
		const uint64_t before = m_machine.now();
		if (m_cycleTarget > before)
			m_machine.run(m_cycleTarget - before);
		// The FDC keeps its own time in the states the CPU consumed; its DRQ lands bytes
		// in RAM from here.
		m_fdc.advance(static_cast<uint32_t>(m_machine.now() - before));
	}

	Mv30::SampleFrame Mv30::renderAudioFrame()
	{
		const auto voices = m_lp.renderSample();
		std::array<int32_t, lpLib::LP::VoiceCount> tvfIn{};
		std::array<int32_t, lpLib::LP::VoiceCount> tvfOut{};
		for (size_t v = 0; v < voices.size(); ++v)
			tvfIn[v] = busTap(voices[v], 21);
		m_tvf.processFrame<lpLib::LP::VoiceCount>(tvfIn.data(), tvfOut.data());

		// The TVF to RCC link is serial and four slots deep: each slot arrives with the
		// sample of the slot four before it, the first four with the last of the previous
		// frame.
		rccLib::RCC::Voices rccIn{};
		for (size_t v = 0; v < rccIn.size(); ++v)
			rccIn[v] = std::exchange(m_rccSerialDelay[v & 3], busTap(tvfOut[v], 23));

		const auto output = m_rcc.processFrame(rccIn);
		if (!output || m_outputMute)
			return {};
		// The firmware puts MIX L/R on RCC slots 6 and 7. Signed 16-bit DAC words, scaled to
		// the boards' common 24-bit full scale.
		return {output->serialLoadWords[6] * 256, output->serialLoadWords[7] * 256};
	}

	Mv30::SampleFrame Mv30::renderSample()
	{
		if (!m_valid)
			return {};
		++m_samplesRendered;
		m_midiIn->processSample();
		if (++m_panelTickPhase >= g_panelTickSamples)
		{
			m_panelTickPhase = 0;
			panelTick();
		}
		runCpuForSample();
		m_lcd.flush();
		return renderAudioFrame();
	}

	// =====================================================================
	// MIDI
	// =====================================================================

	void Mv30::addMidiEvent(const synthLib::SMidiEvent& _event)
	{
		if (m_midiIn)
			m_midiIn->write(synthLib::SMidiEvent(_event));
	}

	void Mv30::readMidiOut(std::vector<synthLib::SMidiEvent>& _events)
	{
		m_midiOut.getEvents(_events);
	}

	void Mv30::transportDiscontinuity(const uint32_t _generation)
	{
		if (m_midiIn)
			m_midiIn->transportDiscontinuity(_generation);
	}
}
