#include "88lib/boards/cm32l.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace emu88Lib
{
	namespace
	{
		// The VCA's control voltage is the CPU's PWM smoothed by R39 82k into C44 0.1uF.
		constexpr float g_vcaTimeConstant = 82e3f * 0.1e-6f;

		// The reverb return's share of the low-pass summing node, 6.8k / 10k.
		constexpr float g_reverbReturn = 6.8f / 10.0f;
	}

	Cm32l::Cm32l(const Cm32lRomSet& roms)
		: m_control(roms.isValid() ? roms.control : std::vector<uint8_t>{}), m_reverb(roms.reverb),
		  m_ramHigh(0x4000, 0)
	{
		m_valid = roms.isValid();
		if(!m_valid) return;
		auto& bus = m_machine.bus();
		bus.map_device(0x0000, 0x1000, &m_host);	// registers, switches, LA32
		bus.load(0x1000, m_control.data() + 0x1000, 0x7000);
		bus.map_rom(0x1000, 0x7000);
		bus.map_device(0x8000, 0x4000, &m_host);	// bank window
		bus.map_ram(DirectRamBase, DirectRamSize);
		m_machine.periph().set_serial_tx_byte_hook([this](const uint8_t value) { m_midiOut.write(value); });
		reset();
	}

	void Cm32l::reset()
	{
		if(!m_valid) return;
		std::memset(directRam(), 0, DirectRamSize);
		std::fill(m_ramHigh.begin(), m_ramHigh.end(), 0);
		m_lcdBuffer.clear();
		m_buttons = {0xff, 0xff};
		m_bank = m_controlLatch = m_reverbTime = m_reverbLevel = m_port0 = 0;
		m_cpuRemainder = 0;
		// C44 starts discharged, so the board fades up out of silence as the firmware takes
		// the PWM down - the same soft start the real one makes.
		m_vcaGain = 0.0f;
		m_midiOut = synthLib::MidiBufferParser(synthLib::MidiEventSource::Device);
		m_machine.cpu().select_code_bank(0x8000, 0x4000, 0);
		m_lcd.reset();
		m_reverb.reset();
		m_midiIn = std::make_unique<synthLib::MidiRateLimiter>(
			[this](const uint8_t value) { m_machine.periph().receive_serial(value); });
		m_midiIn->setSamplerate(SampleRate);
		m_midiIn->setRateLimit(3125);
		m_midiIn->setPreserveEventOrder(true);
		m_machine.reset();
		auto& periph = m_machine.periph();
		periph.set_port_input(0, m_port0);
		periph.set_port_input(2, 0xff);
		// The MT-32's value knob is an analogue input; the CM-32L leaves the pin tied high.
		periph.set_analog_input(7, 1023);
	}

	uint8_t Cm32l::leds() const
	{
		// MIDI MESSAGE is bit 0 of the control latch, active high. It is the only output the
		// board drives that ever moves, so there is no other candidate for the lamp. The
		// firmware holds it up while MIDI is arriving - a burst of controllers lights it for
		// about as long again after the last byte - and for as long as a note is sounding.
		return m_valid && (m_controlLatch & 0x01) ? 1 : 0;
	}

	void Cm32l::setButtons(const uint32_t buttons)
	{
		for(uint32_t button = 0; button < ButtonCount; ++button)
		{
			const auto mask = static_cast<uint8_t>(1u << (button & 7));
			auto& group = m_buttons[button >> 3];
			if(buttons & (1u << button)) group &= static_cast<uint8_t>(~mask); else group |= mask;
		}
	}

	uint8_t Cm32l::readBank(const uint16_t offset) const
	{
		// The control ROM pages in linearly across the four banks it fills.
		if(static_cast<size_t>(m_bank) * 0x4000 < m_control.size())
			return m_control[static_cast<size_t>(m_bank) * 0x4000 + offset];
		if(m_bank == 0x10) return directRam()[offset];
		if(m_bank == 0x11) return m_ramHigh[offset];
		return 0xff;
	}

	void Cm32l::writeBank(const uint16_t offset, const uint8_t value)
	{
		if(m_bank == 0x10) m_machine.bus().write8(DirectRamBase + offset, value);
		else if(m_bank == 0x11) m_ramHigh[offset] = value;
		else return;
		m_machine.cpu().code_written(static_cast<uint16_t>(0x8000 + offset));
	}

	uint8_t Cm32l::read(const uint16_t address)
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
		// Byte-wide LA32 addressing. The MT-32 board wires it word-wide and answers
		// 0C00-0FFF, one register per address pair.
		if(address >= 0x0c00 && address < 0x0e00) return 0xff;
		if(address >= 0x8000 && address < 0xc000) return readBank(address - 0x8000);
		return 0xff;
	}

	void Cm32l::write(const uint16_t address, const uint8_t value)
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
		if(address >= 0x0300 && address <= 0x037f)
		{
			if(m_lcdBuffer.size() < 256) m_lcdBuffer.push_back(value);
			return;
		}
		if(address >= 0x0380 && address <= 0x03ff) { flushLcd(value); return; }
		if(address == 0x0400) { m_reverbTime = value; updateReverbParameters(); return; }
		if(address == 0x0800) { m_reverbLevel = value; updateReverbParameters(); return; }
		if(address >= 0x0c00 && address < 0x0e00) { return; }
		if(address >= 0x8000 && address < 0xc000) writeBank(address - 0x8000, value);
	}

	void Cm32l::flushLcd(const uint8_t control)
	{
		// The firmware queues the data bytes into the latch and then writes the control byte,
		// which strobes the whole burst into the controller.
		m_lcd.writeControl(control);
		for(const auto data : m_lcdBuffer) m_lcd.writeData(data);
		m_lcdBuffer.clear();
	}

	void Cm32l::updateReverbParameters()
	{
		// The reverb parameters are spread over three registers. On this board generation the
		// time is 0400h bits 2-0, the level 0800h bits 1-0 over 0400h bit 3, and the mode the
		// control latch's bit 1 over 0800h bit 2. The older MT-32 board wires all three one
		// bit higher in the same registers.
		const unsigned time = m_reverbTime & 7;
		const unsigned level = ((m_reverbLevel & 3) << 1) | ((m_reverbTime >> 3) & 1);
		const unsigned mode = (m_controlLatch & 2) | ((m_reverbLevel >> 2) & 1);
		// Reverb ROM A14 is strapped high. The CM-32L ROM mirrors both halves, so the strap is
		// a no-op here; on the MT-32 it selects the half holding the four live mode programs.
		m_reverb.setParameters(4 | mode, time, level);
	}

	float Cm32l::applyVca()
	{
		// The 8095 drives the VCA with a PWM whose duty is the attenuation: the firmware takes
		// it from 0 at master volume 70 up to 255 at volume 0, linearly, and leaves it at 0
		// above 70 where it scales the partials digitally instead. Over that span the part and
		// the firmware's ramp together make master volume linear in amplitude, which a control
		// voltage taken straight as the gain reproduces to a few percent; the M5207L01's own
		// transfer curve has not been measured.
		const auto duty = static_cast<float>(m_machine.periph().pwm_duty());
		const auto target = 1.0f - duty * (1.0f / 255.0f);
		const auto alpha = 1.0f - std::exp(-1.0f / (static_cast<float>(SampleRate) * g_vcaTimeConstant));
		m_vcaGain += (target - m_vcaGain) * alpha;
		return m_vcaGain;
	}

	Cm32l::SampleFrame Cm32l::renderSample()
	{
		if(!m_valid) return {};
		m_midiIn->processSample();
		// Clock the CPU alongside the LA32's 32 time-multiplexed partial slots. This also
		// presents the real LA32 SH3 level on P0.4: SH3 changes twice per 32 kHz output frame,
		// rather than once per completed sample.
		for(unsigned slot = 0; slot < 32; ++slot)
		{
			// A slot is worth under four states, so one instruction usually covers several
			// slots: run only once the debt is paid off.
			m_cpuRemainder += CpuStateRate;
			if(m_cpuRemainder >= static_cast<int64_t>(La32SlotRate))
				m_cpuRemainder -= static_cast<int64_t>(
					m_machine.run(static_cast<uint64_t>(m_cpuRemainder / La32SlotRate))) * La32SlotRate;
		}
		// The LA32 and the reverb share the digital audio bus. The firmware assigns
		// reverb-enabled partials to SYN1 (pair 2) and dry-only partials to SYN2 (pair 3).
		// During the SYN1 slots the same data is latched by both the analogue dry S/H and the
		// reverb input; the reverb drives its wet L/R samples during the REV slots. The
		// physical latch sequence is L REV, R REV, L SYN2, L SYN1, R SYN2, R SYN1.
		const auto buses = std::array<int32_t, 8>{};
		const auto wet = m_reverb.renderFrame({buses[2], buses[6]});
		// The three channels of a side meet at the summing node of their low-pass, each through
		// its own resistor: 6.8k for both SYN pairs and 10k for the reverb return, so the wet
		// path arrives 6.8/10 down on the dry one. (Left: R74, R73, R75; right: R72, R70, R71.)
		const auto mix = [](const int32_t _syn1, const int32_t _syn2, const int32_t _rev)
		{
			return static_cast<float>(_syn1 + _syn2) + static_cast<float>(_rev) * g_reverbReturn;
		};
		const auto gain = applyVca();
		const auto left = mix(buses[2], buses[3], wet.first) * gain;
		const auto right = mix(buses[6], buses[7], wet.second) * gain;
		// Scale the signed 16-bit DAC words to the shared board interface's 24-bit full scale.
		const auto scale = [](const float _v)
		{
			return static_cast<int32_t>(std::clamp(_v, -32768.0f, 32767.0f)) * 256;
		};
		return {scale(left), scale(right)};
	}

	void Cm32l::addMidiEvent(const synthLib::SMidiEvent& event, const uint8_t port)
	{
		if(m_midiIn && port == 0) m_midiIn->write(synthLib::SMidiEvent(event));
	}

	void Cm32l::readMidiOut(std::vector<synthLib::SMidiEvent>& events)
	{
		m_midiOut.getEvents(events);
	}

	void Cm32l::transportDiscontinuity(const uint32_t generation)
	{
		if(m_midiIn) m_midiIn->transportDiscontinuity(generation);
	}
}
