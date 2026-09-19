#include "88lib/boards/nu10b.h"

namespace emu88Lib
{
	using sh2::Bus;

	namespace
	{
		constexpr uint32_t g_dramBase = 0x01000000;
		constexpr uint32_t g_dramSize = 0x20000;
		constexpr uint32_t g_programBase = 0x02000000;
		constexpr uint32_t g_xpBase = 0x04000000;
		constexpr uint32_t g_gaBase = 0x04380000;

		constexpr unsigned g_irqGateArray = 5;
		constexpr unsigned g_irqXp = 7;

		// Gate-array registers. The ones below 0x04 are the switch-matrix rows.
		constexpr uint8_t g_gaScanRows = 4;
		constexpr uint8_t g_gaPortIn0 = 0x3a;
		constexpr uint8_t g_gaPortIn1 = 0x3b;
		constexpr uint8_t g_gaIrqStatus = 0x3c;
		constexpr uint8_t g_gaIrqKey = 0x3e;

		// What the status register reports.
		constexpr uint8_t g_irqKey = 0x00;
		constexpr uint8_t g_irqControlTick = 0x08;
		constexpr uint8_t g_irqTimerTick = 0x09;

		// The RTOS tick runs from power-on. The 100 Hz control pass paces the LFOs, and the
		// boot code is not ready for it during its first three seconds.
		constexpr uint64_t g_timerTickStates = Nu10b::CpuClockHz / 1000;
		constexpr uint64_t g_controlTickStates = Nu10b::CpuClockHz / 100;
		constexpr uint64_t g_controlTickHoldOff = 3 * uint64_t{Nu10b::CpuClockHz};

		// One display instruction per DREQ0 pulse, about 40 us.
		constexpr uint64_t g_lcdDmaByteStates = 800;

		// Key codes, switch-matrix row * 8 + column. The gate array reports a press with
		// bit 7 set and a release without it.
		constexpr uint8_t g_keyUtility = 0x04;
		constexpr uint8_t g_keyPerform = 0x06;
		constexpr uint8_t g_keyPatch = 0x0e;
		constexpr uint8_t g_keyCursorRight = 0x1c;
		constexpr uint8_t g_keyShift = 0x1d;
		constexpr uint8_t g_keyEnter = 0x1f;

		// The panel is scanned at 100 Hz: a key has to stay closed across a scan to count as
		// a press and open across another to count as a release.
		constexpr uint32_t g_keyHold = Nu10b::SampleRate * 40 / 1000;
		constexpr uint32_t g_keyGap = Nu10b::SampleRate * 90 / 1000;
	}

	Nu10b::Nu10b(const std::vector<uint8_t>& _cpuRom, const std::vector<uint8_t>& _programRom,
	             std::vector<uint8_t> _waveRom, const bool _factoryReset)
		: m_waveRom(std::move(_waveRom))
	{
		if(_cpuRom.size() != CpuRomSize || _programRom.size() != ProgramRomSize || m_waveRom.size() != WaveRomSize)
			return;

		auto& bus = m_machine.bus();
		bus.load(0, _cpuRom.data(), _cpuRom.size());
		bus.map_ram(g_dramBase, g_dramSize, Bus::kClsDram);
		for(uint32_t address = g_dramBase + g_dramSize; address < g_dramBase + 0x01000000; address += g_dramSize)
			bus.mirror(address, g_dramSize, g_dramBase);
		bus.map_rom(g_programBase, ProgramRomSize, Bus::kClsCs1);
		bus.load(g_programBase, _programRom.data(), _programRom.size());
		// Card window, work SRAM and battery SRAM. The card slot stays empty.
		bus.map_ram(0x02280000, 0x80000, Bus::kClsCs1);
		bus.map_ram(0x02300000, 0x10000, Bus::kClsCs1);
		bus.map_ram(0x02380000, 0x10000, Bus::kClsCs1);
		bus.map_device(g_xpBase, 0x4000, &m_xpDevice, Bus::kClsCs3);
		bus.map_device(g_gaBase, Bus::kLineSize, &m_gaDevice, Bus::kClsCs3);
		bus.set_noexec(g_xpBase, 0x4000);
		bus.set_noexec(g_gaBase, Bus::kLineSize);
		for(uint32_t area = 1; area <= 4; ++area)
			bus.mirror(0x08000000 + area * 0x01000000, 0x01000000, area * 0x01000000);
		m_machine.cpu().invalidate_all();

		m_xp.mapWaveRom(0, m_waveRom.data(), m_waveRom.size(), xpLib::XP::PhysicalWaveRomWidth::bits16);
		m_xp.setInterruptCallback([this](const bool _level) { m_machine.set_irq_pin(g_irqXp, _level); });
		m_machine.sci(0).set_tx_sink([this](const uint8_t _value, bool, uint64_t) { m_midiOut.write(_value); });
		// AN1 watches the backup battery, which the firmware accepts from 0x1ff to 0x2cc;
		// AN0 is a memory card's battery, and no card is fitted.
		m_machine.adc().set_sampler([](const unsigned _channel) -> uint16_t { return _channel == 1 ? 0x266 : 0; });

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

	Nu10b::~Nu10b()
	{
		cancelEvents();
	}

	void Nu10b::cancelEvents()
	{
		for(auto* event : {&m_timerTickEvent, &m_controlTickEvent, &m_lcdDmaEvent})
		{
			if(*event) m_machine.sched().cancel(*event);
			*event = 0;
		}
	}

	void Nu10b::reset()
	{
		cancelEvents();
		m_machine.reset();
		// The card-sense inputs on port B: no card in any slot.
		m_machine.ports().set_input_b(0);
		m_xp.reset();
		m_gaRegisters.fill(0);
		m_keyEvents.clear();
		m_heldKeys = 0;
		m_irqPending = m_timerTickPending = m_controlTickPending = false;
		// The machine's state counter is monotonic across a reset: the sample pacing
		// re-anchors to it.
		m_cycleTarget = m_machine.now();
		m_timerTickEvent = m_machine.sched().schedule(m_cycleTarget + g_timerTickStates, &onTimerTick, this);
		m_controlTickEvent = m_machine.sched().schedule(m_cycleTarget + g_controlTickHoldOff, &onControlTick, this);
	}

	// =====================================================================
	// Setup
	// =====================================================================

	void Nu10b::boot(const bool _factoryReset)
	{
		// A blind timeline of key presses, as a user would type them, rather than watching
		// firmware state. Nothing is pressed before the control tick starts and the intro
		// has given way to the play screen.
		run(4 * SampleRate);

		if(_factoryReset)
		{
			// UTILITY, seven pages right to FACTORY, ENTER to arm and ENTER to execute. User
			// memory starts write protected, so that asks for WRITE=[UTILITY] or CANCEL=[EXIT]:
			// UTILITY writes, the unit shows COMPLETE, and PATCH returns to the play screen.
			tap(g_keyUtility);
			for(int i = 0; i < 7; ++i)
				tap(g_keyCursorRight);
			tap(g_keyEnter);
			tap(g_keyEnter);
			run(SampleRate * 3 / 2);
			tap(g_keyUtility);
			run(SampleRate);
			tap(g_keyPatch);
			run(SampleRate * 7 / 10);
		}

		setKey(g_keyShift, true);
		run(g_keyHold);
		tap(g_keyPerform);
		setKey(g_keyShift, false);
		run(SampleRate);

		// Nothing the setup sent out belongs to the song.
		std::vector<synthLib::SMidiEvent> discarded;
		m_midiOut.getEvents(discarded);
	}

	void Nu10b::run(uint32_t _samples)
	{
		while(_samples-- > 0)
			renderSample();
	}

	void Nu10b::tap(const uint8_t _code)
	{
		setKey(_code, true);
		run(g_keyHold);
		setKey(_code, false);
		run(g_keyGap);
	}

	void Nu10b::setKey(const uint8_t _code, const bool _pressed)
	{
		if(_pressed)
			m_heldKeys |= 1u << _code;
		else
			m_heldKeys &= ~(1u << _code);
		m_keyEvents.push_back(static_cast<uint8_t>(_pressed ? (_code | 0x80) : _code));
		if(!m_irqPending)
			deliverIrq();
	}

	// =====================================================================
	// Gate array
	// =====================================================================

	uint8_t Nu10b::gateArrayRead(const uint8_t _reg)
	{
		if(_reg < g_gaScanRows)
			return static_cast<uint8_t>(m_heldKeys >> (_reg * 8));

		const uint8_t value = m_gaRegisters[_reg];
		switch(_reg)
		{
		case g_gaPortIn0:
			return 0xfe; // PREVIEW, INC and DEC, active low; bit 0 drives an LED
		case g_gaPortIn1:
			return 0xff; // the VALUE dial's switch
		case g_gaIrqStatus:
			// A tick is acknowledged by its status, a key only once its code is read.
			if(m_irqPending && value != g_irqKey)
				acknowledgeIrq();
			break;
		case g_gaIrqKey:
			if(m_irqPending && m_gaRegisters[g_gaIrqStatus] == g_irqKey)
				acknowledgeIrq();
			break;
		default:
			break;
		}
		return value;
	}

	// Everything shares IRQ5, which stays asserted until the firmware has taken the event;
	// keys go first, then the control tick, then the RTOS tick.
	void Nu10b::deliverIrq()
	{
		if(!m_keyEvents.empty())
		{
			m_gaRegisters[g_gaIrqStatus] = g_irqKey;
			m_gaRegisters[g_gaIrqKey] = m_keyEvents.front();
			m_keyEvents.pop_front();
		}
		else if(m_controlTickPending)
		{
			m_controlTickPending = false;
			m_gaRegisters[g_gaIrqStatus] = g_irqControlTick;
		}
		else if(m_timerTickPending)
		{
			m_timerTickPending = false;
			m_gaRegisters[g_gaIrqStatus] = g_irqTimerTick;
		}
		else
		{
			return;
		}
		m_irqPending = true;
		m_machine.set_irq_pin(g_irqGateArray, true);
	}

	void Nu10b::acknowledgeIrq()
	{
		m_irqPending = false;
		m_machine.set_irq_pin(g_irqGateArray, false);
		deliverIrq();
	}

	void Nu10b::onTimerTick(void* _self, const uint64_t _when, uint64_t)
	{
		auto* board = static_cast<Nu10b*>(_self);
		board->m_timerTickPending = true;
		if(!board->m_irqPending)
			board->deliverIrq();
		board->m_timerTickEvent = board->m_machine.sched().schedule(_when + g_timerTickStates, &onTimerTick, board);
	}

	void Nu10b::onControlTick(void* _self, const uint64_t _when, uint64_t)
	{
		auto* board = static_cast<Nu10b*>(_self);
		board->m_controlTickPending = true;
		if(!board->m_irqPending)
			board->deliverIrq();
		board->m_controlTickEvent = board->m_machine.sched().schedule(_when + g_controlTickStates, &onControlTick, board);
	}

	// The display is written by DMAC channel 0 in external-request mode, aimed at the gate
	// array's two display registers; the gate array pulses DREQ0 once per byte.
	bool Nu10b::lcdDmaWanted()
	{
		auto& dmac = m_machine.dmac();
		if(!dmac.enabled(0) || ((dmac.chcr(0) >> sh2::Dmac7034::kRsShift) & 0xf) != 0)
			return false;
		const uint32_t target = dmac.dar(0) & 0x07ffffff;
		return target >= g_gaBase && target < g_gaBase + 0x40;
	}

	void Nu10b::onLcdDma(void* _self, const uint64_t _when, uint64_t)
	{
		auto* board = static_cast<Nu10b*>(_self);
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

	void Nu10b::addMidiEvent(const synthLib::SMidiEvent& _event)
	{
		m_midiIn->write(synthLib::SMidiEvent{_event});
	}

	void Nu10b::readMidiOut(std::vector<synthLib::SMidiEvent>& _events)
	{
		m_midiOut.getEvents(_events);
	}

	void Nu10b::transportDiscontinuity(const uint32_t _generation)
	{
		m_midiIn->transportDiscontinuity(_generation);
	}

	// =====================================================================
	// Frame
	// =====================================================================

	Nu10b::SampleFrame Nu10b::renderSample()
	{
		if(!m_valid)
			return {0, 0};

		m_midiIn->processSample();
		if(!m_lcdDmaEvent && lcdDmaWanted())
			m_lcdDmaEvent = m_machine.sched().schedule(m_machine.now() + 1, &onLcdDma, this);

		// One audio frame of the CPU clock, exactly 625 states.
		m_cycleTarget += CpuClockHz / SampleRate;
		while(m_machine.now() < m_cycleTarget)
			m_machine.run(m_cycleTarget - m_machine.now());

		m_xp.step();
		const auto& dsp = m_xp.dsp();
		const auto& output = dsp.serialOutput(xpLib::Dsp::SerialBus::b);
		return dsp.serialOutputCount(xpLib::Dsp::SerialBus::b) >= 2 ? SampleFrame{output[0], output[1]}
		                                                            : SampleFrame{0, 0};
	}
}
