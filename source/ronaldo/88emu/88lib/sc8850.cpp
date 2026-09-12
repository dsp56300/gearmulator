#include "sc8850.h"
#include "baseLib/md5.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace
{
	std::vector<uint8_t> combineWaveRoms(const std::vector<uint8_t>& _rom0, const std::vector<uint8_t>& _rom1)
	{
		if(_rom0.empty() && _rom1.empty()) return {};
		static constexpr size_t PhysicalRomSize = 0x01000000;
		std::vector<uint8_t> result(PhysicalRomSize * 2, 0);
		std::copy_n(_rom0.begin(), std::min(_rom0.size(), PhysicalRomSize), result.begin());
		std::copy_n(_rom1.begin(), std::min(_rom1.size(), PhysicalRomSize), result.begin() + PhysicalRomSize);
		return result;
	}
}

namespace emu88Lib
{
	using sh2::Bus;

	namespace
	{
		struct MatrixPos
		{
			uint8_t ss;
			uint8_t pd;
		};

		// The first 19 entries are SW1..SW19 from the switch-board schematic.
		// VOLSW and ENCSW are direct gate-array inputs represented by the two
		// otherwise unused PD7 key codes. The stock source-0 ISR explicitly
		// reverses bit 7 for codes 0x07 and 0x0f because these pins are active
		// high, unlike the diode matrix.
		constexpr std::array<MatrixPos, static_cast<size_t>(Sc8850Button::Count)> g_panelPositions{{
			{1, 0}, {2, 0}, {3, 0}, {1, 1}, {2, 1}, {3, 1}, {1, 2}, {2, 2}, {3, 2}, {1, 3}, {2, 3},
			{3, 3}, {3, 4}, {3, 5}, {0, 4}, {0, 3}, {0, 2}, {0, 1}, {0, 0}, {0, 7}, // VALUE-encoder push / ENCSW
			{1, 7}																	// volume-pot push: PREVIEW / VOLSW
		}};

		constexpr uint32_t g_panelButtonCount = static_cast<uint32_t>(Sc8850Button::Count);
		constexpr uint32_t g_panelButtonMask = (1u << g_panelButtonCount) - 1u;

		// Gate-array registers 0x1a / 0x1b start the two periodic sources
		// (10 = voice control at 125 Hz, 11 = ROM Play control at 1 kHz).
		constexpr uint8_t g_gateTimerEnable[2] = {0x1a, 0x1b};
		constexpr uint8_t g_gateTimerSource[2] = {10, 11};
		constexpr uint64_t g_gateTimerPeriod[2] = {Sc8850::CpuClockHz / Sc8850::VoiceControlRate,
		                                          Sc8850::CpuClockHz / Sc8850::RomPlayControlRate};
	}

	Sc8850::Sc8850(std::vector<uint8_t> _cpuRom, std::vector<uint8_t> _programRom,
				   std::vector<uint8_t> _dataRom, const std::vector<uint8_t>& _waveRom0,
				   const std::vector<uint8_t>& _waveRom1, const bool _factoryReset)
		: m_cpuRom(std::move(_cpuRom))
		, m_programRom(std::move(_programRom))
		, m_dataRom(std::move(_dataRom))
		, m_waveRom(combineWaveRoms(_waveRom0, _waveRom1))
	{
		if(m_cpuRom.size() != CpuRomSize)
		{
			std::fprintf(stderr, "Sc8850: CPU ROM must be %u bytes, got %zu\n", CpuRomSize, m_cpuRom.size());
			return;
		}
		if(m_programRom.size() != ProgramRomSize)
		{
			std::fprintf(stderr, "Sc8850: program flash must be %u bytes, got %zu\n", ProgramRomSize,
						 m_programRom.size());
			return;
		}
		if(m_dataRom.size() != DataRomSize)
		{
			std::fprintf(stderr, "Sc8850: data flash must be %u bytes, got %zu\n", DataRomSize, m_dataRom.size());
			return;
		}
		m_autoVoiceReset = baseLib::MD5(m_cpuRom) == baseLib::MD5("efe1ffb0ccbe1b2ec454692c522494fc")
			&& baseLib::MD5(m_programRom) == baseLib::MD5("554d5997dcd9ce6fa0777092ff48f6fa")
			&& baseLib::MD5(m_dataRom) == baseLib::MD5("06eee65647b66109efb01eabd6d71248");

		// ---- bus map ----
		Bus& bus = m_machine.bus();
		bus.load(0, m_cpuRom.data(), m_cpuRom.size());
		// IC9, LH28F800: the 1 MiB executable flash on CS0, the upper half of
		// the window being the schematic's shadow.  Reads come from the bus image
		// while the array is readable; writes are always commands.
		bus.map_rom_device(ProgramFlashBase, 0x200000, &m_flashDevice, Bus::kClsCs0);
		bus.load(ProgramFlashBase, m_programRom.data(), m_programRom.size());
		bus.load(ProgramFlashBase + ProgramRomSize, m_programRom.data(), m_programRom.size());
		bus.map_ram(0x00400000u, 0x400000, Bus::kClsCs1);
		bus.map_ram(0x00800000u, 0x400000, Bus::kClsCs2);
		bus.map_ram(0x00c00000u, 0x100000, Bus::kClsCs3);
		// IC10, LH28F160: the 2 MiB system-program/tone-parameter flash on CS3.
		bus.map_rom(0x00d00000u, 0x200000, Bus::kClsCs3);
		bus.load(0x00d00000u, m_dataRom.data(), m_dataRom.size());
		bus.mirror(0x00f00000u, 0x100000, 0x00c00000u); // CS3's upper shadow
		bus.map_ram(WorkRamBase, WorkRamSize, Bus::kClsDram);
		// Peripherals decoded inside the RAM windows.
		bus.map_device(0x00500000u, Bus::kLineSize, &m_lcdDevice, Bus::kClsCs1);
		bus.map_device(0x00540000u, Bus::kLineSize, &m_usbDevice, Bus::kClsCs1);
		bus.map_device(0x00580000u, Bus::kLineSize, &m_usbDevice, Bus::kClsCs1);
		bus.map_device(0x005c0000u, Bus::kLineSize, &m_lspDevice, Bus::kClsCs1);
		bus.map_device(0x006c0000u, Bus::kLineSize, &m_gaDevice, Bus::kClsCs1);
		bus.map_device(0x00a00000u, 0x4000, &m_xpDevice0, Bus::kClsCs2);
		bus.map_device(0x00a80000u, 0x4000, &m_xpDevice1, Bus::kClsCs2);
		m_machine.cpu().invalidate_all();

		if(m_waveRom.size() >= 0x2000000)
		{
			for(auto& xp : m_xp)
			{
				for(size_t chip = 0; chip < xpLib::XP::nWaveChipSelects; ++chip)
				{
					const size_t physicalOffset = chip * 0x400000;
					xp.mapWaveRom(chip, m_waveRom.data() + physicalOffset, 0x400000,
								  xpLib::XP::PhysicalWaveRomWidth::bits16, 2, 0);
				}
			}
		}
		// The schematic and the installed vector table agree: XP0INT and XP1INT
		// go directly to SH7016 IRQ0 and IRQ1. IRQ2 is reserved for the gate
		// array's multiplexed event output.
		m_xp[0].setInterruptCallback([this](const bool _level) { m_machine.intc().set_irq_pin(0, _level); });
		m_xp[1].setInterruptCallback([this](const bool _level) { m_machine.intc().set_irq_pin(1, _level); });
		m_gateArray.setIrqCallback([this](const bool _level) { m_machine.intc().set_irq_pin(2, _level); });
		m_usb.receiveInterrupt = [this] { return queueGateIrq(hwLib::Tc160g22af::externalIrqSource(1)); };
		m_usb.transmitInterrupt = [this] { return queueGateIrq(hwLib::Tc160g22af::externalIrqSource(0)); };
		m_xpEnabled = {!_waveRom0.empty() || !_waveRom1.empty(), !_waveRom0.empty() || !_waveRom1.empty()};
		// MIDI OUT 2 is SCI0, MIDI OUT 1 is SCI1.
		for(unsigned channel = 0; channel < 2; ++channel)
			m_machine.sci(channel).set_tx_sink([this, channel](const uint8_t _value, bool, uint64_t)
			{ m_midiOut[channel].push_back(_value); });

		m_valid = true;
		reset();
		if(_factoryReset) runFactoryReset();
	}

	Sc8850::~Sc8850()
	{
		if(m_lcdDmaEvent) m_machine.sched().cancel(m_lcdDmaEvent);
		for(auto& e : m_gateTimerEvent)
			if(e) m_machine.sched().cancel(e);
	}

	// =====================================================================
	// Factory reset
	// =====================================================================

	void Sc8850::runFactoryReset()
	{
		if(!m_valid) return;

		// Use the owner's-manual shortcut instead of coupling this setup pass to
		// the menu layout:
		//
		//   hold SHIFT and press PART < -> "Initialize Sure?"
		//   press ENTER                  -> execute
		//
		// Press SHIFT first and release it last so the gate-array event stream
		// contains the same ordering as a physical chord.
		constexpr uint32_t kBootWait = 8 * SampleRate;
		constexpr uint32_t kModifierLead = SampleRate / 20; // 50 ms
		constexpr uint32_t kChordHold = SampleRate / 10;	// 100 ms
		constexpr uint32_t kKeyGap = SampleRate / 4;		// 250 ms
		constexpr uint32_t kExecuteHold = SampleRate / 10;	// 100 ms
		constexpr uint32_t kExecuteSettle = 2 * SampleRate;

		const auto run = [this](uint32_t _samples)
		{
			while(_samples-- > 0)
				renderSample();
		};
		const auto set = [this](const Sc8850Button _button, const bool _pressed)
		{ setButton(static_cast<uint32_t>(_button), _pressed); };

		// Nobody listens to a setup pass, so neither XP is clocked for its
		// duration - the same argument as Sc88::runFactoryReset: no voices run,
		// XP0INT/XP1INT stay silent, and the program-flash image this writes is
		// byte-identical with both chips idle.
		const auto xpEnabled = m_xpEnabled;
		const auto lspEnabled = m_lspEnabled;
		m_xpEnabled = {false, false};
		m_lspEnabled = false;

		run(kBootWait);
		set(Sc8850Button::Shift, true);
		run(kModifierLead);
		set(Sc8850Button::PartLeft, true);
		run(kChordHold);
		set(Sc8850Button::PartLeft, false);
		run(kModifierLead);
		set(Sc8850Button::Shift, false);
		run(kKeyGap);

		set(Sc8850Button::Enter, true);
		run(kExecuteHold);
		set(Sc8850Button::Enter, false);
		run(kExecuteSettle);

		m_xpEnabled = xpEnabled;
		m_lspEnabled = lspEnabled;

		// Reboot from the freshly initialized program-flash settings.
		reset();
	}

	void Sc8850::powerCycle()
	{
		if(m_lcdDmaEvent) m_machine.sched().cancel(m_lcdDmaEvent);
		m_lcdDmaEvent = 0;
		for(auto& e : m_gateTimerEvent)
		{
			if(e) m_machine.sched().cancel(e);
			e = 0;
		}
		for(auto& xp : m_xp)
			xp.reset();
		m_lsp.clear();
		m_voiceResetSamples = 0;
		m_lcd.reset();
		m_gateArray.reset();
		m_gateIrqPending = false;
		m_ledPortWritten = false;
		m_gateQueuedSources = 0;
		m_panelButtons = 0;
		m_panelEventCursor = 0;
		m_panelEvents.clear();
		m_encoderEvents.clear();
		m_usb.reset();
		m_midiOut[0].clear();
		m_midiOut[1].clear();
		m_xp0SdiaInput.fill(0);
		m_xp0SdibInput.fill(0);
		m_xp1SdiaInput.fill(0);
		setFlashMode(FlashMode::ReadArray);
		m_programFlashStatus = 0x0080;
		m_programFlashPendingAddress = 0xffffffff;
		m_programFlashPendingHigh = 0xff;
		m_machine.reset();
		setMidiTransport(m_midiTransport);
	}

	void Sc8850::reset()
	{
		powerCycle();
		// The machine's state counter is monotonic across a reset: the sample
		// pacing re-anchors to it.
		m_cycleTarget = m_machine.now();
	}

	void Sc8850::setMidiTransport(const MidiTransport _transport)
	{
		m_midiTransport = _transport;
		// SELV is wired to AN0 through the four-position USB/PC/Mac/MIDI rear
		// selector: USB is VCC/full scale and MIDI is ground. At ground the stock
		// firmware selects its physical-DIN configuration and does not create the
		// USB receive queue.
		m_machine.adc_mid_speed().set_input(0, _transport == MidiTransport::Usb ? 0x03ff : 0);
	}

	void Sc8850::sciMidiIn(const uint8_t _port, const uint8_t _value) { m_machine.sci(_port & 1).receive_byte(_value); }

	void Sc8850::readMidiOut(std::vector<uint8_t>& _output) { m_usb.readMidiOut(_output); }

	void Sc8850::readPhysicalMidiOut(std::vector<uint8_t>& _output)
	{
		// Physical routing is channel 0/TXD0 -> MIDI OUT 2 and channel 1/TXD1
		// -> MTX1 -> MIDI OUT 1. BoardAdapter currently exposes one host output,
		// so merge both physical transmitters while preserving their byte order.
		for(const unsigned channel : {0u, 1u})
		{
			_output.insert(_output.end(), m_midiOut[channel].begin(), m_midiOut[channel].end());
			m_midiOut[channel].clear();
		}
	}

	// =====================================================================
	// Front panel / gate array
	// =====================================================================

	void Sc8850::setButton(const uint32_t _switchIndex, const bool _pressed)
	{
		if(_switchIndex >= g_panelButtonCount) return;
		const uint32_t mask = 1u << _switchIndex;
		if(((m_panelButtons & mask) != 0) == _pressed) return;
		if(_pressed)
			m_panelButtons |= mask;
		else
			m_panelButtons &= ~mask;
		queuePanelEvent(_switchIndex, _pressed);
	}

	void Sc8850::setButtons(const uint32_t _bitmap)
	{
		const uint32_t next = _bitmap & g_panelButtonMask;
		const uint32_t changed = next ^ m_panelButtons;
		m_panelButtons = next;
		for(uint32_t index = 0; index < g_panelButtonCount; ++index)
			if(changed & (1u << index)) queuePanelEvent(index, (next & (1u << index)) != 0);
	}

	uint8_t Sc8850::gateArrayRead(const uint8_t _reg)
	{
		// The SC-8850 uses a TC160G22AF-1253 gate array.
		// The interrupt multiplexer reports source 0 for a key packet and source 1
		// for the encoder; data-less sources acknowledge on the status
		// read. IRQ2 is held active until the matching payload/status is read.
		if(_reg == hwLib::Tc160g22af::RegisterKeyData)
		{
			if(m_gateIrqPending &&
			   m_gateArray.read(hwLib::Tc160g22af::RegisterIrqSource) == hwLib::Tc160g22af::IrqSourceKey)
			{
				const uint8_t value = m_gateArray.read(_reg);
				acknowledgeGateIrq();
				return value;
			}
			// Before IRQs are enabled the boot scanner polls this register and
			// expects successive matrix positions rather than an event packet.
			return m_gateIrqPending ? m_gateArray.read(_reg) : panelEvent();
		}
		if(_reg == hwLib::Tc160g22af::RegisterEncoderDelta && m_gateIrqPending &&
		   m_gateArray.read(hwLib::Tc160g22af::RegisterIrqSource) == hwLib::Tc160g22af::IrqSourceEncoder)
		{
			const uint8_t value = m_gateArray.read(_reg);
			acknowledgeGateIrq();
			return value;
		}
		if(_reg == hwLib::Tc160g22af::RegisterIrqSource)
		{
			const uint8_t value = m_gateIrqPending ? m_gateArray.read(_reg) : 0;
			if(m_gateIrqPending && value != 0 && value != 1) acknowledgeGateIrq();
			return value;
		}
		return m_gateArray.read(_reg);
	}

	void Sc8850::gateArrayWrite(const uint8_t _reg, const uint8_t _val)
	{
		m_gateArray.write(_reg, _val);
		if(_reg == 0x12 || _reg == 0x13) m_usb.kickInterrupts();
		if(_reg == hwLib::Tc160g22af::RegisterLcdCommand || _reg == hwLib::Tc160g22af::RegisterLcdData)
			m_ledPortWritten = true;
		// Nonzero writes here are a boot-order heuristic, not proven per-source
		// interrupt enables. Sources 10/11 drive voice control and ROM Play.
		for(unsigned timer = 0; timer < 2; ++timer)
			if(_reg == g_gateTimerEnable[timer] && _val) armGateTimer(timer);
	}

	void Sc8850::armGateTimer(const unsigned _timer)
	{
		if(m_gateTimerEvent[_timer]) return;
		m_gateTimerEvent[_timer] = m_machine.sched().schedule(m_machine.now() + g_gateTimerPeriod[_timer],
		                                                      _timer == 0 ? &onGateTimer1 : &onGateTimer2, this);
	}

	void Sc8850::onGateTimer1(void* _self, const uint64_t _when, uint64_t) { static_cast<Sc8850*>(_self)->gateTimerFired(0, _when); }
	void Sc8850::onGateTimer2(void* _self, const uint64_t _when, uint64_t) { static_cast<Sc8850*>(_self)->gateTimerFired(1, _when); }

	void Sc8850::gateTimerFired(const unsigned _timer, const uint64_t _when)
	{
		m_gateTimerEvent[_timer] = 0;
		if(!m_gateArray.read(g_gateTimerEnable[_timer])) return; // disabled: stops until the enable is written again
		queueGateIrq(g_gateTimerSource[_timer]);
		m_gateTimerEvent[_timer] = m_machine.sched().schedule(_when + g_gateTimerPeriod[_timer],
		                                                      _timer == 0 ? &onGateTimer1 : &onGateTimer2, this);
	}

	bool Sc8850::queueGateIrq(const uint8_t _source)
	{
		if(_source < hwLib::Tc160g22af::IrqSourceCount && !m_gateArray.read(0x10 + _source)) return false;
		if(m_gateIrqPending)
		{
			m_gateQueuedSources |= static_cast<uint16_t>(1u << _source);
			return true;
		}
		m_gateArray.write(hwLib::Tc160g22af::RegisterIrqSource, _source);
		m_gateIrqPending = true;
		m_gateArray.setIrqLevel(true);
		return true;
	}

	void Sc8850::acknowledgeGateIrq()
	{
		m_gateIrqPending = false;
		m_gateArray.setIrqLevel(false);
		if(!m_panelEvents.empty())
		{
			m_gateArray.write(hwLib::Tc160g22af::RegisterKeyData, m_panelEvents.front());
			m_panelEvents.pop_front();
			queueGateIrq(hwLib::Tc160g22af::IrqSourceKey);
			return;
		}
		if(!m_encoderEvents.empty())
		{
			m_gateArray.write(hwLib::Tc160g22af::RegisterEncoderDelta,
			                 static_cast<uint8_t>(m_encoderEvents.front()));
			m_encoderEvents.pop_front();
			queueGateIrq(hwLib::Tc160g22af::IrqSourceEncoder);
			return;
		}
		if(m_gateQueuedSources)
		{
			uint8_t source = 0;
			while(!(m_gateQueuedSources & (1u << source)))
				++source;
			m_gateQueuedSources &= static_cast<uint16_t>(~(1u << source));
			queueGateIrq(source);
		}
	}

	void Sc8850::queuePanelEvent(const uint32_t _switchIndex, const bool _pressed)
	{
		if(!m_gateArray.read(0x10)) return;
		const auto [ss, pd] = g_panelPositions[_switchIndex];
		const bool directInput = pd == 7;
		const uint8_t packet = static_cast<uint8_t>((_pressed == directInput ? 0x80 : 0x00) | (ss << 3) | pd);
		if(m_gateIrqPending)
		{
			if(m_panelEvents.size() < 32) m_panelEvents.push_back(packet);
			return;
		}
		m_gateArray.write(hwLib::Tc160g22af::RegisterKeyData, packet);
		queueGateIrq(hwLib::Tc160g22af::IrqSourceKey);
	}

	void Sc8850::turnEncoder(const int8_t _delta)
	{
		if(!_delta || !m_gateArray.read(0x11)) return;
		if(m_gateIrqPending)
		{
			if(m_encoderEvents.size() < 32) m_encoderEvents.push_back(_delta);
			return;
		}
		m_gateArray.write(hwLib::Tc160g22af::RegisterEncoderDelta, static_cast<uint8_t>(_delta));
		queueGateIrq(hwLib::Tc160g22af::IrqSourceEncoder);
	}

	uint8_t Sc8850::leds() const
	{
		if(!m_ledPortWritten) return 0;
		const auto data = m_gateArray.read(hwLib::Tc160g22af::RegisterLcdData);
		const auto command = m_gateArray.read(hwLib::Tc160g22af::RegisterLcdCommand);
		const auto bit = [](const uint8_t _port, const uint8_t _portBit, const Led _led)
		{
			return (_port & (1u << _portBit)) ? 0u : (1u << static_cast<uint8_t>(_led));
		};
		return static_cast<uint8_t>(bit(data, 0, Led::Edit) | bit(data, 1, Led::Drum) | bit(data, 2, Led::Effects) |
		                            bit(command, 2, Led::Shift) | bit(command, 0, Led::Solo) |
		                            bit(command, 1, Led::Mute));
	}

	uint8_t Sc8850::panelEvent()
	{
		// Switch numbering follows the service schematic (SW1..SW19, row-major
		// on the front panel). Each packet carries SS in bits 6..3 and PD in
		// bits 2..0; bit 7 is one for a released switch and zero for a pressed
		// switch. Codes 7 and 15 are the active-high direct inputs, so their bit-7
		// sense is deliberately the opposite. The boot ROM collects the four scan
		// bytes and its source-0 ISR applies that same special-case inversion.
		uint8_t scan[4] = {0x7f, 0x7f, 0xff, 0xff};
		for(uint8_t index = 0; index < g_panelButtonCount; ++index)
		{
			if(m_panelButtons & (1u << index))
			{
				const auto [ss, pd] = g_panelPositions[index];
				if(pd == 7)
					scan[ss] |= 0x80;
				else
					scan[ss] &= static_cast<uint8_t>(~(1u << pd));
			}
		}

		const uint8_t index = m_panelEventCursor;
		m_panelEventCursor = static_cast<uint8_t>((m_panelEventCursor + 1) & 31);
		const uint8_t ss = index >> 3;
		const uint8_t pd = index & 7;
		const bool released = (scan[ss] & (1u << pd)) != 0;
		return static_cast<uint8_t>((released ? 0x80 : 0x00) | (ss << 3) | pd);
	}

	// =====================================================================
	// USB, display DMA
	// =====================================================================

	uint32_t Sc8850::workRamRead32(const uint32_t _address)
	{
		if(_address < WorkRamBase || _address + 3 >= WorkRamBase + WorkRamSize) return 0;
		return m_machine.bus().read32(_address);
	}

	void Sc8850::tickUsb()
	{
		if(!m_usb.ready()) return;
		// The stock USB task dequeues 32-bit event packets from RTOS queue 5.
		// Its queue table base is stored at 0x105334c; entry 5 is 0x68 bytes
		// wide and uses the read/write indices at +0x10/+0x12. Requesting a
		// source-2 event only while those indices differ avoids an idle IRQ
		// storm and precisely mirrors the controller asking for queued data.
		const uint32_t table = workRamRead32(0x0105334c);
		const uint32_t queue = table + 5 * 0x68;
		if(table < WorkRamBase || queue + 0x15 >= WorkRamBase + WorkRamSize) return;
		const uint32_t data = workRamRead32(queue + 0x0c);
		const uint32_t indices = workRamRead32(queue + 0x10);
		const uint16_t readIndex = static_cast<uint16_t>(indices >> 16);
		const uint16_t writeIndex = static_cast<uint16_t>(indices);
		const uint16_t capacity = static_cast<uint16_t>(workRamRead32(queue + 0x14) >> 16);
		if(data < WorkRamBase || data >= WorkRamBase + WorkRamSize || !capacity || readIndex >= capacity ||
		   writeIndex >= capacity)
			return;
		if(readIndex != writeIndex) m_usb.requestTransmit();
	}

	// DMAC channel 1 in external-request mode with the LCD data port as destination.
	bool Sc8850::lcdDmaWanted() const
	{
		const sh2::Dmac& dmac = const_cast<sh2::Machine&>(m_machine).dmac();
		const unsigned rs = (dmac.chcr(1) >> sh2::Dmac::kRsShift) & 0xf; // 0, 2, 3 = external request
		return dmac.enabled(1) && (rs == 0 || rs == 2 || rs == 3) && dmac.dar(1) == 0x00500000u;
	}

	void Sc8850::onLcdDma(void* _self, const uint64_t _when, uint64_t) { static_cast<Sc8850*>(_self)->lcdDmaTick(_when); }

	void Sc8850::lcdDmaTick(const uint64_t _when)
	{
		m_lcdDmaEvent = 0;
		if(!lcdDmaWanted()) return;
		m_machine.dmac().set_dreq(1, true);
		m_machine.dmac().set_dreq(1, false);
		m_lcdDmaEvent = m_machine.sched().schedule(_when + LcdDmaByteStates, &onLcdDma, this);
	}

	// =====================================================================
	// Program flash (LH28F800)
	// =====================================================================

	void Sc8850::setFlashMode(const FlashMode _mode)
	{
		const bool wasArray = m_programFlashMode == FlashMode::ReadArray;
		m_programFlashMode = _mode;
		const bool isArray = _mode == FlashMode::ReadArray;
		if(wasArray == isArray) return;
		// In a command mode the array answers with status / identifier words, so
		// reads have to go through the device; in read mode they come straight
		// from memory. Both mappings share the access class and neither touches
		// the array, so decoded code stays valid across the swap - only a real
		// program or erase invalidates, see invalidateProgramFlash().
		Bus& bus = m_machine.bus();
		if(isArray) bus.map_rom_device(ProgramFlashBase, 0x200000, &m_flashDevice, Bus::kClsCs0);
		else bus.map_device(ProgramFlashBase, 0x200000, &m_flashDevice, Bus::kClsCs0);
	}

	// The flash is executable, so changing it does drop the decoded code - but
	// only for the bytes that changed, at each address the array is mirrored to.
	void Sc8850::invalidateProgramFlash(const uint32_t _offset, const uint32_t _size)
	{
		for(uint32_t mirror = 0; mirror < 0x200000; mirror += ProgramRomSize)
			m_machine.cpu().invalidate_range(ProgramFlashBase + mirror + _offset, _size);
	}

	void Sc8850::storeProgramByte(const uint32_t _offset, const uint8_t _value)
	{
		m_programRom[_offset] = _value;
		for(uint32_t mirror = 0; mirror < 0x200000; mirror += ProgramRomSize)
			*m_machine.bus().ptr(ProgramFlashBase + mirror + _offset) = _value;
	}

	uint8_t Sc8850::programFlashRead(const uint32_t _addr) const
	{
		const uint32_t offset = (_addr - ProgramFlashBase) & (ProgramRomSize - 1);
		uint16_t word = 0xffff;
		switch(m_programFlashMode)
		{
		case FlashMode::ReadArray:
			return m_programRom[offset];
		case FlashMode::ReadId:
			// Sharp LH28F800SGE-L70: manufacturer B0h, device 50h. The
			// mask-ROM updater compares these two 16-bit words verbatim.
			word = (offset & ~1u) == 0 ? 0x00b0 : ((offset & ~1u) == 2 ? 0x0050 : 0xffff);
			break;
		case FlashMode::ReadStatus:
		case FlashMode::EraseSetup:
		case FlashMode::ProgramSetup:
		case FlashMode::LockSetup:
			word = m_programFlashStatus;
			break;
		}
		return static_cast<uint8_t>((offset & 1) ? word : (word >> 8));
	}

	void Sc8850::programFlashWrite(const uint32_t _addr, const uint8_t _value)
	{
		const uint32_t offset = (_addr - ProgramFlashBase) & (ProgramRomSize - 1);
		if(!(offset & 1))
		{
			m_programFlashPendingAddress = offset;
			m_programFlashPendingHigh = _value;
			return;
		}
		const uint32_t even = offset - 1;
		const uint8_t high = m_programFlashPendingAddress == even ? m_programFlashPendingHigh : 0;
		m_programFlashPendingAddress = 0xffffffff;
		programFlashWriteWord(even, static_cast<uint16_t>((uint16_t(high) << 8) | _value));
	}

	void Sc8850::programFlashWriteWord(const uint32_t _offset, const uint16_t _value)
	{
		if(m_programFlashMode == FlashMode::ProgramSetup)
		{
			// Flash programming can change one bits to zero only. This also
			// catches an updater sequence that accidentally skipped erase.
			storeProgramByte(_offset, m_programRom[_offset] & static_cast<uint8_t>(_value >> 8));
			storeProgramByte(_offset + 1, m_programRom[_offset + 1] & static_cast<uint8_t>(_value));
			invalidateProgramFlash(_offset, 2);
			m_programFlashStatus = 0x0080;
			setFlashMode(FlashMode::ReadStatus);
			return;
		}

		switch(static_cast<uint8_t>(_value))
		{
		case 0xff: setFlashMode(FlashMode::ReadArray); break;
		case 0x90: setFlashMode(FlashMode::ReadId); break;
		case 0x70: setFlashMode(FlashMode::ReadStatus); break;
		case 0x50:
			m_programFlashStatus = 0x0080;
			setFlashMode(FlashMode::ReadStatus);
			break;
		case 0x20: setFlashMode(FlashMode::EraseSetup); break;
		case 0x40: setFlashMode(FlashMode::ProgramSetup); break;
		case 0x60: setFlashMode(FlashMode::LockSetup); break;
		case 0xd0:
			if(m_programFlashMode == FlashMode::EraseSetup)
			{
				const uint32_t block = _offset & ~(ProgramEraseBlockSize - 1u);
				for(uint32_t i = 0; i < ProgramEraseBlockSize; ++i) storeProgramByte(block + i, 0xff);
				invalidateProgramFlash(block, ProgramEraseBlockSize);
			}
			m_programFlashStatus = 0x0080;
			setFlashMode(FlashMode::ReadStatus);
			break;
		default:
			m_programFlashStatus = 0x00a0;
			setFlashMode(FlashMode::ReadStatus);
			break;
		}
	}

	// =====================================================================
	// Frame
	// =====================================================================

	void Sc8850::resetFinishedVoices()
	{
		if(!m_autoVoiceReset)
			return;
		// SH-2 routine 0x6604 marks a completed allocation free. The
		// envelope accumulator can retain a nonzero value after completion.
		const auto* records = m_machine.bus().ptr(WorkRamBase + 0x22024);
		for(unsigned voice = 0; voice < 128; ++voice)
		{
			const auto* record = records + voice * 0x1e4;
			if(record[0] != voice || record[1] != 1 || record[0x148] != 0)
				continue;
			m_xp[voice < 64 ? 1 : 0].retireVoice(voice & 63);
		}
	}

	Sc8850::SampleFrame Sc8850::renderSample()
	{
		if(!m_valid) return {0, 0};

		m_usb.tick();
		tickUsb();
		if(!m_lcdDmaEvent && lcdDmaWanted())
			m_lcdDmaEvent = m_machine.sched().schedule(m_machine.now() + 1, &onLcdDma, this);

		// One audio frame of the CPU clock (exactly 882 states).  The machine
		// slices the CPU to its peripheral events and the gate-array timers.
		m_cycleTarget += StatesPerSample;
		while(m_machine.now() < m_cycleTarget)
			m_machine.run(m_cycleTarget - m_machine.now());

		if((++m_voiceResetSamples & 127u) == 0)
			resetFinishedVoices();

		m_lcd.flush();
		auto& xp0Dsp = m_xp[0].dsp();
		auto& xp1Dsp = m_xp[1].dsp();
		xp0Dsp.setSerialInput(xpLib::Dsp::SerialBus::a, m_xp0SdiaInput.data(), m_xp0SdiaInput.size());
		xp0Dsp.setSerialInput(xpLib::Dsp::SerialBus::b, m_xp0SdibInput.data(), m_xp0SdibInput.size());
		xp1Dsp.setSerialInput(xpLib::Dsp::SerialBus::a, m_xp1SdiaInput.data(), m_xp1SdiaInput.size());
		if(m_xpEnabled[0]) m_xp[0].step();
		if(m_xpEnabled[1]) m_xp[1].step();

		// XP0 is the auxiliary chip. Its two SDOC half-pass words feed the LSP.
		// The held SDIB node returns right at slot 0 and left at slot 128.
		// Four later mode-0 reads consume XP1's first four SDOA words.
		const auto& xp0Sdoc = xp0Dsp.serialOutput(xpLib::Dsp::SerialBus::c);
		const auto lspOutput =
			m_lspEnabled && xp0Dsp.serialOutputCount(xpLib::Dsp::SerialBus::c) >= 2
				? m_lsp.process({xp0Sdoc[0], xp0Sdoc[1]})
				: SampleFrame{0, 0};
		const auto& xp1Serial = xp1Dsp.serialOutput(xpLib::Dsp::SerialBus::a);
		// The 0x40 descriptor used for this SDIB link has the same eight-bit
		// LSP-to-XP alignment as the Pro's SDIA return. The receive nodes hold
		// signed 24-bit XP words, while the LSP result occupies bits 31-8.
		m_xp0SdibInput = {{
			lspOutput.first >> 8,
			lspOutput.second >> 8,
		}};
		for(size_t i = 0; i < m_xp0SdiaInput.size(); ++i)
			m_xp0SdiaInput[i] = xp1Dsp.serialOutputCount(xpLib::Dsp::SerialBus::a) > i ? xp1Serial[i] : 0;

		// XP1 is the DAC/master chip. Stage the complete boundary stream. Its
		// seven function-C reads sample every boundary except index 1.
		const auto& xp0Serial = xp0Dsp.serialOutput(xpLib::Dsp::SerialBus::a);
		for(size_t i = 0; i < m_xp1SdiaInput.size(); ++i)
			m_xp1SdiaInput[i] = xp0Dsp.serialOutputCount(xpLib::Dsp::SerialBus::a) > i ? xp0Serial[i] : 0;

		const auto& output = xp1Dsp.serialOutput(xpLib::Dsp::SerialBus::c);
		return m_xpEnabled[1] && xp1Dsp.serialOutputCount(xpLib::Dsp::SerialBus::c) >= 2
				   ? SampleFrame{xpLib::XP::serialWordToOutput(output[0]), xpLib::XP::serialWordToOutput(output[1])}
				   : SampleFrame{0, 0};
	}
}
