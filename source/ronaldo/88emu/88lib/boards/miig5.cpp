#include "88lib/boards/miig5.h"

namespace emu88Lib
{
	using sh2::Bus;
	using Ga = hwLib::Tc160g22af;

	namespace
	{
		constexpr uint32_t g_xpBase = 0x00200000;
		constexpr uint32_t g_xpWindow = 0x80000;
		constexpr uint32_t g_gaBase = 0x006c0000;
		constexpr uint32_t g_sramBase = 0x00800000;
		constexpr uint32_t g_sramSize = 0x80000;
		constexpr uint32_t g_programBase = 0x00d00000;

		constexpr unsigned g_irqGateArray = 0;
		constexpr unsigned g_irqXp0 = 1;
		constexpr unsigned g_irqXp1 = 2;

		// The periodic sources. The firmware enables them in register 0x3F just before it
		// unmasks IRQ0 - bit 0 the 1 kHz tick, bit 1 source 7 - and keys and the encoder in
		// 0x3E. Source 7 at 10 Hz is what the firmware's active-sensing service expects.
		constexpr uint8_t g_sourceActiveSensing = 7;
		constexpr uint8_t g_sourceTick = 9;
		constexpr uint8_t g_gaSourceEnables = 0x3e;
		constexpr uint8_t g_gaTimerEnables = 0x3f;
		constexpr uint64_t g_tickStates = Miig5::CpuClockHz / 1000;
		constexpr uint64_t g_activeSensingStates = Miig5::CpuClockHz / 10;
		constexpr uint64_t g_panelScanStates = Miig5::CpuClockHz / 120;

		// One display instruction per DREQ0 pulse, about 40 us.
		constexpr uint64_t g_lcdDmaByteStates = 1320;

		// Key codes, switch-matrix row * 8 + column. The gate array reports a press with
		// bit 7 set and a release without it.
		constexpr uint8_t g_keyGm = 0x04;
		constexpr uint8_t g_keyUtility = 0x17;
		constexpr uint8_t g_keyDec = 0x1a;
		constexpr uint8_t g_keyCursorRight = 0x1c;
		constexpr uint8_t g_keyCursorDown = 0x23;
		constexpr uint8_t g_keyEnter = 0x26;

		constexpr uint32_t g_keyHold = Miig5::SampleRate * 40 / 1000;
		constexpr uint32_t g_keyGap = Miig5::SampleRate * 160 / 1000;
	}

	Miig5::Miig5(const std::vector<uint8_t>& _cpuRom, const std::vector<uint8_t>& _programRom,
	             std::vector<uint8_t> _waveRom, const bool _factoryReset)
		: m_waveRom(std::move(_waveRom))
	{
		if(_cpuRom.size() != CpuRomSize || _programRom.size() != ProgramRomSize || m_waveRom.size() != WaveRomSize)
			return;

		auto& bus = m_machine.bus();
		bus.load(0, _cpuRom.data(), _cpuRom.size());
		bus.map_device(g_xpBase, g_xpWindow, &m_xpDevice0, Bus::kClsCs0);
		bus.map_device(g_xpBase + g_xpWindow, g_xpWindow, &m_xpDevice1, Bus::kClsCs0);
		bus.set_noexec(g_xpBase, 2 * g_xpWindow);
		bus.map_ram(0x00400000, 0x400000, Bus::kClsCs1);
		bus.map_device(g_gaBase, 0x1000, &m_gaDevice, Bus::kClsCs1);
		bus.set_noexec(g_gaBase, 0x1000);
		bus.map_ram(g_sramBase, g_sramSize, Bus::kClsCs2);
		for(uint32_t address = g_sramBase + g_sramSize; address < g_sramBase + 0x400000; address += g_sramSize)
			bus.mirror(address, g_sramSize, g_sramBase);
		bus.map_rom(g_programBase, ProgramRomSize, Bus::kClsCs3);
		bus.load(g_programBase, _programRom.data(), _programRom.size());
		bus.map_ram(0x01000000, 0x400000, Bus::kClsDram);
		m_machine.cpu().invalidate_all();

		constexpr size_t chipSelectSize = WaveRomSize / 2;
		for(auto& xp : m_xp)
		{
			xp.mapWaveRom(0, m_waveRom.data(), chipSelectSize, xpLib::XP::PhysicalWaveRomWidth::bits16);
			xp.mapWaveRom(1, m_waveRom.data() + chipSelectSize, chipSelectSize, xpLib::XP::PhysicalWaveRomWidth::bits16);
		}
		m_xp[0].setInterruptCallback([this](const bool _level) { m_machine.intc().set_irq_pin(g_irqXp0, _level); });
		m_xp[1].setInterruptCallback([this](const bool _level) { m_machine.intc().set_irq_pin(g_irqXp1, _level); });
		m_gateArray.setIrqCallback([this](const bool _level) { m_machine.intc().set_irq_pin(g_irqGateArray, _level); });
		m_machine.sci(0).set_tx_sink([this](const uint8_t _value, bool, uint64_t) { m_midiOut.write(_value); });

		m_midiIn = std::make_unique<synthLib::MidiRateLimiter>([this](const uint8_t _value)
		{
			m_machine.sci(0).receive_byte(_value);
		});
		m_midiIn->setSamplerate(SampleRate);
		m_midiIn->setRateLimit(3125);
		m_midiIn->setPreserveEventOrder(true);
		m_midiIn->setResetPause(0.05f);

		m_valid = true;
		reset();
		boot(_factoryReset);
	}

	Miig5::~Miig5()
	{
		cancelEvents();
	}

	void Miig5::cancelEvents()
	{
		for(auto* event : {&m_tickEvent, &m_activeSensingEvent, &m_panelScanEvent, &m_lcdDmaEvent})
		{
			if(*event) m_machine.sched().cancel(*event);
			*event = 0;
		}
	}

	void Miig5::reset()
	{
		cancelEvents();
		m_machine.reset();
		// Every analog input reads a healthy battery. Port E's unwired pins read low, apart
		// from PE14 and PE15, which are pulled up.
		for(unsigned channel = 0; channel < 4; ++channel)
		{
			m_machine.adc_mid_speed().set_input(channel, 0x266);
			m_machine.adc_mid_speed1().set_input(channel, 0x266);
		}
		m_machine.ports7042().set_input(sh2::Ports7042::Port::E, 0xc000);
		m_gateArray.reset();
		for(auto& xp : m_xp)
			xp.reset();
		m_pendingSources = 0;
		m_keyEvents.clear();
		m_keyBusy = false;
		// The machine's state counter is monotonic across a reset: the sample pacing
		// re-anchors to it.
		m_cycleTarget = m_machine.now();
		m_cycleFraction = 0;
		auto& scheduler = m_machine.sched();
		m_tickEvent = scheduler.schedule(m_cycleTarget + g_tickStates, &onTick, this);
		m_activeSensingEvent = scheduler.schedule(m_cycleTarget + g_activeSensingStates, &onActiveSensing, this);
		m_panelScanEvent = scheduler.schedule(m_cycleTarget + g_panelScanStates, &onPanelScan, this);
	}

	// =====================================================================
	// Setup
	// =====================================================================

	void Miig5::boot(const bool _factoryReset)
	{
		// A blind timeline of key presses, as a user would type them, rather than watching
		// firmware state.
		run(8 * SampleRate);

		if(_factoryReset)
		{
			// UTILITY, down to the second page and right to FACTORY RESET, then ENTER. On an
			// empty battery SRAM the second ENTER detours to "Internal Write Protect", which DEC
			// turns off; ENTER goes back, and two more confirm and execute. With the protection
			// already off there is no detour, and the spare keys land on the confirmation and
			// the play screen, where they do nothing.
			tap(g_keyUtility);
			tap(g_keyCursorDown);
			for(int i = 0; i < 3; ++i)
				tap(g_keyCursorRight);
			tap(g_keyEnter);
			tap(g_keyEnter);
			tap(g_keyDec);
			for(int i = 0; i < 3; ++i)
				tap(g_keyEnter);
			// The reset runs for a little over three seconds before it shows COMPLETED and
			// returns to the play screen, and a key pressed meanwhile waits for it.
			run(5 * SampleRate);
		}

		tap(g_keyGm);
		run(SampleRate);

		// Nothing the setup sent out belongs to the song.
		std::vector<synthLib::SMidiEvent> discarded;
		m_midiOut.getEvents(discarded);
	}

	void Miig5::run(uint32_t _samples)
	{
		while(_samples-- > 0)
			renderSample();
	}

	void Miig5::tap(const uint8_t _code)
	{
		m_keyEvents.push_back(static_cast<uint8_t>(_code | 0x80));
		run(g_keyHold);
		m_keyEvents.push_back(_code);
		run(g_keyGap);
	}

	// =====================================================================
	// Gate array
	// =====================================================================

	uint8_t Miig5::gateArrayRead(const uint16_t _offset)
	{
		const auto reg = Ga::registerIndex(_offset);
		switch(reg)
		{
		case Ga::RegisterIrqSource:
		{
			// The highest pending source. Reading it ends the current pulse, and the next
			// source starts a fresh one, so an edge-triggered IRQ0 sees that too.
			uint8_t source = 15;
			while(source && !(m_pendingSources & (1u << source)))
				--source;
			m_pendingSources &= static_cast<uint16_t>(~(1u << source));
			m_gateArray.setIrqLevel(false);
			if(m_pendingSources)
				m_gateArray.setIrqLevel(true);
			return source;
		}
		case 0x3a:
		case 0x3b:
			return 0xff; // the switches wired straight to the gate array, none pressed
		case Ga::RegisterKeyData:
			m_keyBusy = false;
			break;
		default:
			break;
		}
		return m_gateArray.read(reg);
	}

	void Miig5::raiseSource(const uint8_t _source)
	{
		bool enabled = true;
		switch(_source)
		{
		case Ga::IrqSourceKey: enabled = m_gateArray.read(g_gaSourceEnables) & 0x01; break;
		case Ga::IrqSourceEncoder: enabled = m_gateArray.read(g_gaSourceEnables) & 0x02; break;
		case g_sourceTick: enabled = m_gateArray.read(g_gaTimerEnables) & 0x01; break;
		case g_sourceActiveSensing: enabled = m_gateArray.read(g_gaTimerEnables) & 0x02; break;
		default: break;
		}
		if(!enabled)
			return;
		m_pendingSources |= static_cast<uint16_t>(1u << _source);
		m_gateArray.setIrqLevel(true);
	}

	void Miig5::deliverKey()
	{
		if(m_keyBusy || m_keyEvents.empty())
			return;
		m_gateArray.write(Ga::RegisterKeyData, m_keyEvents.front());
		m_keyEvents.pop_front();
		m_keyBusy = true;
		raiseSource(Ga::IrqSourceKey);
	}

	void Miig5::onTick(void* _self, const uint64_t _when, uint64_t)
	{
		auto* board = static_cast<Miig5*>(_self);
		board->raiseSource(g_sourceTick);
		board->m_tickEvent = board->m_machine.sched().schedule(_when + g_tickStates, &onTick, board);
	}

	void Miig5::onActiveSensing(void* _self, const uint64_t _when, uint64_t)
	{
		auto* board = static_cast<Miig5*>(_self);
		board->raiseSource(g_sourceActiveSensing);
		board->m_activeSensingEvent = board->m_machine.sched().schedule(_when + g_activeSensingStates,
		                                                                 &onActiveSensing, board);
	}

	void Miig5::onPanelScan(void* _self, const uint64_t _when, uint64_t)
	{
		auto* board = static_cast<Miig5*>(_self);
		board->deliverKey();
		board->m_panelScanEvent = board->m_machine.sched().schedule(_when + g_panelScanStates, &onPanelScan, board);
	}

	// The display is written by DMAC channel 0 in external-request mode, aimed at the gate
	// array's display registers; the gate array pulses DREQ0 once per byte.
	bool Miig5::lcdDmaWanted()
	{
		auto& dmac = m_machine.dmac();
		const unsigned request = (dmac.chcr(0) >> sh2::Dmac::kRsShift) & 0xf;
		if(!dmac.enabled(0) || !(request == 0 || request == 2 || request == 3))
			return false;
		const uint32_t target = dmac.dar(0);
		return target >= g_gaBase + Ga::RegisterLcdCommand && target <= g_gaBase + Ga::RegisterLcdData;
	}

	void Miig5::onLcdDma(void* _self, const uint64_t _when, uint64_t)
	{
		auto* board = static_cast<Miig5*>(_self);
		board->m_lcdDmaEvent = 0;
		if(!board->lcdDmaWanted())
			return;
		board->m_machine.dmac().set_dreq(0, true);
		board->m_machine.dmac().set_dreq(0, false);
		board->m_lcdDmaEvent = board->m_machine.sched().schedule(_when + g_lcdDmaByteStates, &onLcdDma, board);
	}

	// =====================================================================
	// MIDI
	// =====================================================================

	void Miig5::addMidiEvent(const synthLib::SMidiEvent& _event)
	{
		m_midiIn->write(synthLib::SMidiEvent{_event});
	}

	void Miig5::readMidiOut(std::vector<synthLib::SMidiEvent>& _events)
	{
		m_midiOut.getEvents(_events);
	}

	void Miig5::transportDiscontinuity(const uint32_t _generation)
	{
		m_midiIn->transportDiscontinuity(_generation);
	}

	// =====================================================================
	// Frame
	// =====================================================================

	Miig5::SampleFrame Miig5::renderSample()
	{
		if(!m_valid)
			return {0, 0};

		m_midiIn->processSample();
		if(!m_lcdDmaEvent && lcdDmaWanted())
			m_lcdDmaEvent = m_machine.sched().schedule(m_machine.now() + 1, &onLcdDma, this);

		// 1031.25 states an audio frame; the remainder carries over.
		m_cycleTarget += CpuClockHz / SampleRate;
		m_cycleFraction += CpuClockHz % SampleRate;
		if(m_cycleFraction >= SampleRate)
		{
			m_cycleFraction -= SampleRate;
			++m_cycleTarget;
		}
		while(m_machine.now() < m_cycleTarget)
			m_machine.run(m_cycleTarget - m_machine.now());

		// XP0 owns the DAC serializers; XP1 reaches them over the serial link stepped with it.
		xpLib::XP::stepLinked(m_xp[0], m_xp[1]);
		const auto& dsp = m_xp[0].dsp();
		const auto& output = dsp.serialOutput(xpLib::Dsp::SerialBus::b);
		return dsp.serialOutputCount(xpLib::Dsp::SerialBus::b) >= 2 ? SampleFrame{output[0], output[1]}
		                                                            : SampleFrame{0, 0};
	}
}
