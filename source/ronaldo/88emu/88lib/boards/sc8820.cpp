#include "88lib/boards/sc8820.h"

#include <algorithm>

namespace emu88Lib
{
	Sc8820::Sc8820(const std::vector<uint8_t>& cpuRom, const std::vector<uint8_t>& programRom,
		std::vector<uint8_t> wave0, std::vector<uint8_t> wave1)
		: m_waves{std::move(wave0), std::move(wave1)}
	{
		if((cpuRom.size() != CpuRomSize && cpuRom.size() != ReconstructedCpuRomSize) ||
			programRom.size() != ProgramRomSize ||
			m_waves[0].size() != 0x1000000 || m_waves[1].size() != 0x800000)
			return;

		using Bus = sh2::Bus;
		auto& bus = m_machine.bus();
		// The SH7017 maps 128 KiB of internal flash. Leave the unknown upper half
		// erased when loading the reconstructed 64 KiB image.
		std::vector<uint8_t> internal(CpuRomSize, 0xff);
		std::copy(cpuRom.begin(), cpuRom.end(), internal.begin());
		bus.load(0, internal.data(), internal.size());
		bus.map_rom(0x00d00000, ProgramRomSize, Bus::kClsCs3);
		bus.load(0x00d00000, programRom.data(), programRom.size());
		bus.map_ram(0x01000000, 0x80000, Bus::kClsDram);
		for(uint32_t address = 0x01080000; address < 0x02000000; address += 0x80000)
			bus.mirror(address, 0x80000, 0x01000000);
		bus.map_device(0x00500000, Bus::kLineSize, &m_lspHost, Bus::kClsCs1);
		bus.map_device(0x00540000, Bus::kLineSize, &m_controller, Bus::kClsCs1);
		bus.map_device(0x00580000, Bus::kLineSize, &m_controller, Bus::kClsCs1);
		bus.map_device(0x00900000, 0x4000, &m_xpHost, Bus::kClsCs2);

		for(size_t i = 0; i < m_waves.size(); ++i)
			m_xp.mapWaveRom(i, m_waves[i].data(), m_waves[i].size(), xpLib::XP::PhysicalWaveRomWidth::bits16);
		m_xp.setInterruptCallback([this](bool level) { m_machine.intc().set_irq_pin(0, level); });
		m_machine.sci(0).set_tx_sink([this](uint8_t value, bool, uint64_t) { m_midiOut.write(value); });
		m_machine.ports().set_write_hook([this](sh2::Port port, uint16_t, uint16_t)
		{
			if(port == sh2::Port::A || port == sh2::Port::E) updateLamps();
		});
		m_usb.receiveInterrupt = [this]
		{
			m_machine.intc().set_irq_pin(2, false);
			m_machine.intc().set_irq_pin(2, true);
			return true;
		};
		m_usb.transmitInterrupt = [this]
		{
			m_machine.intc().set_irq_pin(1, false);
			m_machine.intc().set_irq_pin(1, true);
			return true;
		};
		m_valid = true;
		reset();
	}

	void Sc8820::reset()
	{
		if(!m_valid) return;
		m_xp.reset();
		m_lsp.clear();
		m_usb.reset();
		m_lspReturn.fill(0);
		m_lampRows.fill(0);
		m_buttonPins = 0xffff;
		m_midiOut = synthLib::MidiBufferParser{synthLib::MidiEventSource::Device};
		for(uint8_t port = 0; port < m_midiIn.size(); ++port)
		{
			auto& input = m_midiIn[port];
			input = std::make_unique<synthLib::MidiRateLimiter>([this, port](uint8_t value)
			{
				m_usb.midiIn(port, &value, 1);
			});
			input->setSamplerate(SampleRate);
			input->setRateLimit(3125);
			input->setPreserveEventOrder(true);
			input->setResetPause(0.05f);
		}
		m_machine.reset();
		m_machine.ports().set_pins(sh2::Port::E, m_buttonPins);
		m_cycleTarget = m_machine.now();
	}

	void Sc8820::addMidiEvent(const synthLib::SMidiEvent& event, uint8_t port)
	{
		if(port < m_midiIn.size() && m_midiIn[port])
			m_midiIn[port]->write(synthLib::SMidiEvent{event});
	}

	void Sc8820::readMidiOut(std::vector<synthLib::SMidiEvent>& events)
	{
		m_usb.readMidiOut(events);
		m_midiOut.getEvents(events);
	}

	void Sc8820::transportDiscontinuity(uint32_t generation)
	{
		for(auto& input : m_midiIn)
			if(input) input->transportDiscontinuity(generation);
	}

	void Sc8820::setButton(Button button, bool pressed)
	{
		const uint16_t mask = button == Button::InstMap ? 0x40 : 0x80;
		if(pressed) m_buttonPins &= ~mask;
		else m_buttonPins |= mask;
		m_machine.ports().set_pins(sh2::Port::E, m_buttonPins);
	}

	void Sc8820::updateLamps()
	{
		const auto e = m_machine.ports().dr(sh2::Port::E);
		const uint8_t columns = ((e >> 1) & 1) | ((e & 1) << 1) | ((e >> 1) & 4) | ((e & 4) << 1);
		if(!(e & 0x8000)) m_lampRows[0] = columns;
		if(!(e & 0x4000)) m_lampRows[1] = columns;
		if(!(m_machine.ports().dr(sh2::Port::A) & 0x8000)) m_lampRows[2] = columns;
		m_lampRows[2] = (m_lampRows[2] & 0x0f) | ((e & 0x10) ? 0 : 0x10);
	}

	uint16_t Sc8820::leds() const
	{
		return m_lampRows[0] | (m_lampRows[1] << 4) | ((m_lampRows[2] & 2) << 7) |
			((m_lampRows[2] & 8) << 6) | ((m_lampRows[2] & 0x10) << 6);
	}

	Sc8820::SampleFrame Sc8820::renderSample()
	{
		if(!m_valid) return {0, 0};
		for(auto& input : m_midiIn) input->processSample();
		m_usb.tick();
		m_cycleTarget += CpuClockHz / SampleRate;
		while(m_machine.now() < m_cycleTarget) m_machine.run(m_cycleTarget - m_machine.now());

		auto& dsp = m_xp.dsp();
		// PE12 drives IC29, which gates the LSP serial return before XP SDIA5.
		// Low mutes the link; the LSP continues processing while muted.
		const std::array<int32_t, 2> returns = (m_machine.ports().dr(sh2::Port::E) & 0x1000)
			? m_lspReturn : std::array<int32_t, 2>{};
		dsp.setSerialInput(xpLib::Dsp::SerialBus::a, returns.data(), returns.size());
		m_xp.step();
		const auto& send = dsp.serialOutput(xpLib::Dsp::SerialBus::b);
		const auto sendCount = dsp.serialOutputCount(xpLib::Dsp::SerialBus::b);
		const auto effect = m_lsp.process({sendCount > 0 ? send[0] : 0, sendCount > 1 ? send[1] : 0});
		// IC29 is non-inverting. Align the LSP result with the XP's 24-bit input.
		m_lspReturn = {effect.first >> 8, effect.second >> 8};
		const auto& output = dsp.serialOutput(xpLib::Dsp::SerialBus::d);
		return dsp.serialOutputCount(xpLib::Dsp::SerialBus::d) >= 2
			? SampleFrame{output[0], output[1]}
			: SampleFrame{0, 0};
	}
}
