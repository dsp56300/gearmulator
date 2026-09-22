#include "88lib/boards/laBoard.h"
#include "88lib/analog/cmVca.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace emu88Lib
{
	namespace
	{
		// The reverb return's share of the low-pass summing node on the new-type boards,
		// 6.8k / 10k. munt's silicon-traced reverb model finds the old-type board's return at
		// unity next to the dry channels; its summing network has not been read here.
		constexpr float g_reverbReturnNewBoard = 6.8f / 10.0f;
		constexpr float g_reverbReturnOldBoard = 1.0f;

		constexpr mcs96::Variant cpuVariant(const LaModel _model)
		{
			return _model == LaModel::Cm32ln ? mcs96::Variant::I80C196KB : mcs96::Variant::I8x9x;
		}

		constexpr uint32_t cpuStateRate(const LaModel _model)
		{
			return LaBoard::CpuClock / (_model == LaModel::Cm32ln ? 2 : 3);
		}
	}

	int32_t LaBoard::rotateNewBoardBus(const int32_t word)
	{
		const auto w = static_cast<uint16_t>(word);
		return static_cast<int16_t>((w & 0x8000) | ((w << 1) & 0x7ffe) | ((w >> 14) & 1));
	}

	int32_t LaBoard::shiftOldBoardDac(const int32_t word)
	{
		const auto w = static_cast<uint16_t>(word);
		return static_cast<int16_t>((w & 0x8000) | ((w << 1) & 0x7ffe));
	}

	LaBoard::LaBoard(const LaRomSet& roms)
		: m_model(roms.model), m_cpuStateRate(emu88Lib::cpuStateRate(roms.model)),
		  m_control(roms.isValid() ? roms.control : std::vector<uint8_t>{}), m_machine(cpuVariant(roms.model)),
		  m_reverb(roms.reverb), m_ramHigh(0x4000, 0)
	{
		m_valid = roms.isValid();
		if(!m_valid) return;
		auto& bus = m_machine.bus();
		bus.map_device(0x0000, 0x1000, &m_host);	// registers, switches, LA32
		bus.load(0x1000, m_control.data() + 0x1000, 0x7000);
		bus.map_rom(0x1000, 0x7000);
		bus.map_device(0x8000, 0x4000, &m_host);	// bank window
		bus.map_ram(DirectRamBase, DirectRamSize);
		m_la32.setPcmRom(roms.wave);
		m_la32.setIrqCallback([this](const bool level) { m_machine.periph().set_external_interrupt_input(0, level); });
		m_machine.periph().set_serial_tx_byte_hook([this](const uint8_t value) { m_midiOut.write(value); });
		reset();
	}

	void LaBoard::reset()
	{
		if(!m_valid) return;
		std::memset(directRam(), 0, DirectRamSize);
		std::fill(m_ramHigh.begin(), m_ramHigh.end(), 0);
		m_lcdBuffer.clear();
		m_buttons = {0xff, 0xff};
		m_bank = m_controlLatch = m_reverbTime = m_reverbLevel = 0;
		// The old-type board pulls P0.5 up through 22k; nothing has been found to read it.
		m_port0 = isOldBoard() ? 0x20 : 0x00;
		m_cpuRemainder = 0;
		// C44 starts discharged, so the board fades up out of silence as the firmware takes
		// the PWM down - the same soft start the real one makes.
		m_vcaGain = 0.0f;
		m_vcaControl = 0.0f;
		m_analogSample = {};
		m_dac.fill(0);
		m_midiOut = synthLib::MidiBufferParser(synthLib::MidiEventSource::Device);
		m_machine.cpu().select_code_bank(0x8000, 0x4000, 0);
		m_lcd.reset();
		m_la32.reset();
		m_reverb.reset();
		m_midiIn = std::make_unique<synthLib::MidiRateLimiter>(
			[this](const uint8_t value) { m_machine.periph().receive_serial(value); });
		m_midiIn->setSamplerate(SampleRate);
		m_midiIn->setRateLimit(3125);
		m_midiIn->setPreserveEventOrder(true);
		// Nothing on these boards answers All Sound Off; a hanging note is stopped the way
		// the manuals say, hold pedal up and All Notes Off.
		m_midiIn->setSilence(synthLib::MidiRateLimiter::Silence::HoldOffAllNotesOff);
		m_machine.reset();
		auto& periph = m_machine.periph();
		periph.set_port_input(0, m_port0);
		periph.set_port_input(2, 0xff);
		// The MT-32's VOLUME/VALUE knob is a potentiometer on analog channel 7; the CM-32L
		// boards leave the pin tied high. A reset keeps the knob where it was turned to, as a
		// pot would: the hardware reads it wherever it stands.
		periph.set_analog_input(7, m_knob);
	}

	uint8_t LaBoard::leds() const
	{
		// MIDI MESSAGE is bit 0 of the control latch, active high. It is the only output the
		// board drives that ever moves, so there is no other candidate for the lamp. The
		// firmware holds it up while MIDI is arriving - a burst of controllers lights it for
		// about as long again after the last byte - and for as long as a note is sounding.
		return m_valid && (m_controlLatch & 0x01) ? 1 : 0;
	}

	void LaBoard::setButtons(const uint32_t buttons)
	{
		for(uint32_t button = 0; button < ButtonCount; ++button)
		{
			const auto mask = static_cast<uint8_t>(1u << (button & 7));
			auto& group = m_buttons[button >> 3];
			if(buttons & (1u << button)) group &= static_cast<uint8_t>(~mask); else group |= mask;
		}
	}

	void LaBoard::setKnob(const uint16_t position)
	{
		m_knob = std::min(position, KnobMaximum);
		if(m_valid)
			m_machine.periph().set_analog_input(7, m_knob);
	}

	uint8_t LaBoard::readBank(const uint16_t offset) const
	{
		// The control ROM pages in linearly across the banks it fills: four of them for the
		// 64 KiB firmwares, eight for the new-type MT-32 board's 128 KiB, whose upper half is
		// where the ROM Play demo songs live.
		if(static_cast<size_t>(m_bank) * 0x4000 < m_control.size())
			return m_control[static_cast<size_t>(m_bank) * 0x4000 + offset];
		if(m_bank == 0x10) return directRam()[offset];
		if(m_bank == 0x11) return m_ramHigh[offset];
		return 0xff;
	}

	void LaBoard::writeBank(const uint16_t offset, const uint8_t value)
	{
		if(m_bank == 0x10) m_machine.bus().write8(DirectRamBase + offset, value);
		else if(m_bank == 0x11) m_ramHigh[offset] = value;
		else return;
		m_machine.cpu().code_written(static_cast<uint16_t>(0x8000 + offset));
	}

	uint8_t LaBoard::read(const uint16_t address)
	{
		// The switch matrix answers over the whole 0200-027F mirror, two active-low groups
		// selected by address bits 1 and 2.
		if(address >= 0x0200 && address <= 0x027f)
		{
			const auto select = static_cast<uint8_t>((address >> 1) & 15);
			uint8_t result = 0xff;
			if((select & 2) == 0) result &= m_buttons[0];
			if((select & 1) == 0) result &= m_buttons[1];
			return result;
		}
		// The display latch is write-only; its busy flag reads back clear.
		if(address >= 0x0380 && address <= 0x03ff) return 0;
		// The new-type boards address the LA32 a byte at a time over 0C00-0DFF. The old-type
		// board's 16-bit bus leaves the CPU's A0 off the chip - A1-A9 are its A0-A8 - so one
		// register occupies an address pair, over 0C00-0FFF.
		if(isOldBoard())
		{
			if(address >= 0x0c00 && address < 0x1000) return m_la32.read((address - 0x0c00) >> 1);
		}
		else if(address >= 0x0c00 && address < 0x0e00) return m_la32.read(address - 0x0c00);
		if(address >= 0x8000 && address < 0xc000) return readBank(address - 0x8000);
		return 0xff;
	}

	void LaBoard::write(const uint16_t address, const uint8_t value)
	{
		if(address >= 0x0100 && address <= 0x017f)
		{
			m_bank = value;
			m_machine.cpu().select_code_bank(0x8000, 0x4000, value);	// code runs from the window
			return;
		}
		if(address >= 0x0200 && address <= 0x027f)
		{
			m_controlLatch = value;
			updateReverbParameters();
			return;
		}
		// Every firmware writes CAh to 0280h once or twice as it starts and never touches it
		// again; what the gate array keeps there is not known, and taking it as the latch
		// would only flip the reverb program's address bit for the length of the boot.
		if(address >= 0x0280 && address <= 0x02ff)
			return;
		if(address >= 0x0300 && address <= 0x037f)
		{
			if(m_lcdBuffer.size() < 256) m_lcdBuffer.push_back(value);
			return;
		}
		if(address >= 0x0380 && address <= 0x03ff) { flushLcd(value); return; }
		if(address == 0x0400) { m_reverbTime = value; updateReverbParameters(); return; }
		if(address == 0x0800) { m_reverbLevel = value; updateReverbParameters(); return; }
		if(isOldBoard())
		{
			if(address >= 0x0c00 && address < 0x1000) { m_la32.write((address - 0x0c00) >> 1, value); return; }
		}
		else if(address >= 0x0c00 && address < 0x0e00) { m_la32.write(address - 0x0c00, value); return; }
		if(address >= 0x8000 && address < 0xc000) writeBank(address - 0x8000, value);
	}

	void LaBoard::flushLcd(const uint8_t control)
	{
		// The firmware queues the data bytes into the latch and then writes the control byte,
		// which strobes the whole burst into the controller.
		m_lcd.writeControl(control);
		for(const auto data : m_lcdBuffer) m_lcd.writeData(data);
		m_lcdBuffer.clear();
	}

	void LaBoard::updateReverbParameters()
	{
		// The reverb parameters are spread over three registers, and the wiring follows the
		// board generation, one bit apart. On the new-type boards the time is 0400h bits 2-0,
		// the level 0800h bits 1-0 over 0400h bit 3, and the mode the control latch's bit 1
		// over 0800h bit 2. The old-type board's reverb chip sits on CPU D1-D5 rather than
		// AD0-AD4, so it takes all three one bit higher in the same registers.
		unsigned time, level, mode;
		if(isOldBoard())
		{
			time = (m_reverbTime >> 1) & 7;
			level = (m_reverbLevel & 6) | ((m_reverbTime >> 4) & 1);
			mode = ((m_controlLatch >> 1) & 2) | ((m_reverbLevel >> 3) & 1);
		}
		else
		{
			time = m_reverbTime & 7;
			level = ((m_reverbLevel & 3) << 1) | ((m_reverbTime >> 3) & 1);
			mode = (m_controlLatch & 2) | ((m_reverbLevel >> 2) & 1);
		}
		// Reverb ROM A14 is strapped high: the MT-32 microcode keeps its four live mode
		// programs in the upper half, and a running unit never addresses the lower one. The
		// CM-32L's R15179917 mirrors both halves, so the strap is a no-op there.
		m_reverb.setParameters(4 | mode, time, level);
	}

	float LaBoard::applyVca()
	{
		// The old-type board has no VCA: its firmware scales the partials digitally over the
		// whole master volume range and the PWM pin goes nowhere.
		if(!hasVca())
			return 1.0f;
		// Firmware controls PWM in the lower master-volume range and scales partials
		// digitally above it. The measured VCA has a small dead zone near mute.
		m_vcaGain = cmVca::process(m_vcaControl, m_machine.periph().pwm_duty(), cmVca::LaOffsetCounts);
		return m_vcaGain;
	}

	LaBoard::SampleFrame LaBoard::renderSample()
	{
		if(!m_valid) return {};
		m_midiIn->processSample();
		// Clock the CPU alongside the LA32's 32 time-multiplexed partial slots. This also
		// presents the real LA32 SH3 level on P0.4: SH3 changes twice per 32 kHz output frame,
		// rather than once per completed sample.
		for(unsigned slot = 0; slot < 32; ++slot)
		{
			m_port0 = static_cast<uint8_t>((m_port0 & ~0x10) | (m_la32.sh3() << 4));
			m_machine.periph().set_port_input(0, m_port0);
			// A slot is worth under four states, so one instruction usually covers several
			// slots: run only once the debt is paid off.
			m_cpuRemainder += m_cpuStateRate;
			if(m_cpuRemainder >= static_cast<int64_t>(La32SlotRate))
				m_cpuRemainder -= static_cast<int64_t>(
					m_machine.run(static_cast<uint64_t>(m_cpuRemainder / La32SlotRate))) * La32SlotRate;
			m_la32.stepSlot();
		}
		// The LA32 and the reverb share the digital audio bus. The firmware assigns
		// reverb-enabled partials to SYN1 (pair 2) and dry-only partials to SYN2 (pair 3).
		// During the SYN1 slots the same data is latched by both the analogue dry S/H and the
		// reverb input; the reverb drives its wet L/R samples during the REV slots. The
		// physical latch sequence is L REV, R REV, L SYN2, L SYN1, R SYN2, R SYN1.
		//
		// What is on the bus is the chip's output as the board wires it - see
		// rotateNewBoardBus(). The doubling is real: read off the new-type MT-32 schematic,
		// confirmed by munt on a CM-32L, and it is what the CM-64 calibration had been
		// missing, where the LA half came out 5 dB under the resistor analysis of the two
		// boards' output stages with the LA32 word taken straight.
		auto buses = m_la32.currentOutput();
		if(!isOldBoard())
			for(auto& word : buses)
				word = rotateNewBoardBus(word);
		const auto wet = m_reverb.renderFrame({buses[2], buses[6]});
		m_dac = {buses[2], buses[6], buses[3], buses[7], wet.first, wet.second};
		// The old-type board's DAC takes every channel one bit up, see shiftOldBoardDac().
		if(isOldBoard())
			for(auto& word : m_dac)
				word = shiftOldBoardDac(word);
		// The three channels of a side meet at the summing node of their low-pass, each through
		// its own resistor: 6.8k for both SYN pairs and 10k for the reverb return on the
		// new-type boards, so the wet path arrives 6.8/10 down on the dry one. (Left: R74,
		// R73, R75; right: R72, R70, R71.)
		const auto reverbReturn = isOldBoard() ? g_reverbReturnOldBoard : g_reverbReturnNewBoard;
		const auto mix = [reverbReturn](const int32_t _syn1, const int32_t _syn2, const int32_t _rev)
		{
			return static_cast<float>(_syn1 + _syn2) + static_cast<float>(_rev) * reverbReturn;
		};
		const auto gain = applyVca();
		const auto left = mix(m_dac[0], m_dac[2], m_dac[4]) * gain;
		const auto right = mix(m_dac[1], m_dac[3], m_dac[5]) * gain;
		m_analogSample = {left, right};
		// Scale the signed 16-bit DAC words to the shared board interface's 24-bit full scale.
		const auto scale = [](const float _v)
		{
			return static_cast<int32_t>(std::clamp(_v, -32768.0f, 32767.0f)) * 256;
		};
		return {scale(left), scale(right)};
	}

	void LaBoard::addMidiEvent(const synthLib::SMidiEvent& event, const uint8_t port)
	{
		if(m_midiIn && port == 0) m_midiIn->write(synthLib::SMidiEvent(event));
	}

	void LaBoard::readMidiOut(std::vector<synthLib::SMidiEvent>& events)
	{
		m_midiOut.getEvents(events);
	}

	void LaBoard::transportDiscontinuity(const uint32_t generation)
	{
		if(m_midiIn) m_midiIn->transportDiscontinuity(generation);
	}
}
