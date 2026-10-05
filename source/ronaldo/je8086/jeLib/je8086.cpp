#include "je8086.h"

#include <cstring>

#include "rom.h"

#include "synthLib/deviceException.h"

namespace jeLib
{
	namespace
	{
		// MIDI input is held back for the first ~0.8 s, the firmware drops what arrives while it boots
		constexpr uint64_t MidiInputHoldStates = 12'776'184;

		// factory reset button script: first action after ~7 s, when the boot screen is gone, then one action every ~0.16 s
		constexpr uint64_t FactoryResetScriptStart = 112'776'184;
		constexpr uint64_t FactoryResetScriptInterval = 2'600'000;

		const std::vector<devices::GramRoute> g_keyboardRoutes =
		{
			{0, 1, 0x00, 0x04},
			{1, 2, 0x00, 0x0a},
			{2, 3, 0x00, 0x0e},
			{2, 3, 0x20, 0x22}
		};

		// the A/D converter (PCM3001) shifts into ASIC0, where it lands like a GRAM route
		constexpr uint8_t g_rackAdcLeft = 0x60;
		constexpr uint8_t g_rackAdcRight = 0x64;

		const std::vector<devices::GramRoute> g_rackRoutes =
		{
			{0, 1, 0x10, 0x16},
			{1, 2, 0x00, 0x16},
			{2, 3, 0x00, 0x16},
			{3, 4, 0x00, 0x12},
			{4, 0, 0x10, 0x12}
		};

		constexpr uint32_t KeyboardSwitchRows = 32;
	}

	Je8086::Je8086(const std::vector<uint8_t>& _romData, const std::vector<uint8_t>& _ramData)
	: m_isRack(_romData.size() == Rom::RomSizeRack)
	, m_ports(m_isRack ? je8080::SwitchRows : KeyboardSwitchRows)
	, m_faders(m_machine, m_isRack ? je8080::faderChannels() : devices::Faders::keyboardChannels())
	, m_panelLine(&m_catchAll)
	, m_rackLcdLine(&m_catchAll)
	, m_rackStatusLine(&m_catchAll)
	, m_midiOutParser(synthLib::MidiEventSource::Device)
	, m_midiInRateLimiter([this](const uint8_t _byte) { m_machine.periph().receive(0, _byte); })
	{
		m_remote.evButtonChanged.addListener([this](const uint32_t _button, const bool _pressed)
		{
			setButton(static_cast<devices::SwitchType>(_button), _pressed);
		});

		auto& bus = m_machine.bus();
		m_machine.map_rom(0, static_cast<uint32_t>(_romData.size()));
		bus.load(0, _romData.data(), _romData.size());
		m_machine.map_ram(0x200000, RamSize);
		if (_ramData.size() == RamSize)
			bus.load(0x200000, _ramData.data(), _ramData.size());

		// the gap between flash and SRAM, above the SRAM, between the on-chip RAM and the register field
		bus.set_unmapped_device(&m_catchAll);

		if (m_isRack)
		{
			m_rackAsics = std::make_unique<devices::RackAsics>(g_rackRoutes);
			m_rackDintStatus = std::make_unique<devices::DintStatus<devices::RackAsics>>(*m_rackAsics);
			m_machine.map_device(0x400000, devices::RackAsics::Count * devices::RackAsics::WindowSize, m_rackAsics.get());
			m_rackStatusLine.assign(0x2c0001, 1, m_rackDintStatus.get());
			m_machine.map_device(0x2c0000, h8300h::Bus::kLineSize, &m_rackStatusLine);
			m_rackLcdLine.assign(0x41c004, 2, &m_lcd);
			m_machine.map_device(0x41c000, h8300h::Bus::kLineSize, &m_rackLcdLine);
		}
		else
		{
			m_keyboardAsics = std::make_unique<devices::KeyboardAsics>(g_keyboardRoutes);
			m_machine.map_device(0x400000, devices::KeyboardAsics::Count * devices::KeyboardAsics::WindowSize, m_keyboardAsics.get());
			m_panelLine.assign(0x600000, 4, &m_keyScanner);
			m_panelLine.assign(0x600004, 2, &m_lcd);
			m_machine.map_device(0x600000, h8300h::Bus::kLineSize, &m_panelLine);
		}
		m_machine.map_register(0xffffe0, 10, &m_faders);	// A/D
		m_machine.map_register(0xffffcb, 1, &m_faders);		// port 6: fader mux select
		m_machine.map_register(0xffffd3, 2, &m_ports);
		m_machine.map_register(0xffffd6, 1, &m_ports);

		auto& periph = m_machine.periph();
		periph.set_tx_hook(0, [this](const uint8_t _byte) { onReceiveMidiByte(_byte); });
		if (!m_isRack)
			periph.set_tpc_output_hook([this](const uint16_t _value, const uint16_t _mask) { m_ports.setTpcOutput(_value, _mask); });

		m_ports.reset();
		m_faders.reset();
		m_machine.reset();

		if (m_isRack)
		{
			// ASIC0 DINT bit 1 is wired to P94
			auto setIrq1 = [this](const bool _high)
			{
				auto& irqBus = m_machine.bus();
				const uint8_t value = irqBus.read8(0xffffd2);
				irqBus.write8(0xffffd2, static_cast<uint8_t>((value & ~16) | (_high ? 16 : 0)));
			};
			auto& asic0 = m_rackAsics->first();
			setIrq1((asic0.readDint() & 2) != 0);
			asic0.setDintCallback([setIrq1](uint32_t, const uint8_t _before, const uint8_t _after)
			{
				if ((_before ^ _after) & 2)
					setIrq1((_after & 2) != 0);
			});
		}

		m_midiInRateLimiter.setSamplerate(88200.0f);
		m_midiInRateLimiter.setDefaultRateLimit();
		m_midiInRateLimiter.setSysexPause(0.021f);	// the manual asks for 20 ms between SysEx patch messages
		m_midiInRateLimiter.setSysexPauseLengthThreshold(100);
		m_midiInRateLimiter.setSysexFilter([](const synthLib::SysexBuffer& _sysex)
		{
			// neither model implements MTS or Universal Master Fine Tuning
			return !synthLib::isUniversalTuningSysex(_sysex);
		});

		// The firmware rewrites the LCD with the same content on every edit. Each update replaces what the editor
		// shows on it, a parameter readout too, so only content that changed goes out
		m_lcd.setChangeCallback([this]
		{
			if (m_lcd.getContentGeneration() == m_lcdGeneration)
				return;
			m_lcdGeneration = m_lcd.getContentGeneration();
			SysexRemoteControl::sendSysexLcdDdRam(m_midiOutEvents, m_lcd);
		});
		m_lcd.setCgRamChangeCallback([this] { SysexRemoteControl::sendSysexLcdCgRam(m_midiOutEvents, m_lcd); });
	}

	std::vector<uint8_t> Je8086::createFactoryRam(const std::vector<uint8_t>& _romData)
	{
		Je8086 board(_romData, {});
		return board.runFactoryReset();
	}

	void Je8086::addMidiEvent(const synthLib::SMidiEvent& _event)
	{
		if (_event.type == synthLib::MidiEventType::TransportDiscontinuity)
		{
			m_midiInRateLimiter.transportDiscontinuity(_event.transportGeneration);
			return;
		}

		// panel buttons are pressed here, on the emulation thread (BoardThread), in order with the MIDI around them
		if (m_remote.receive(_event))
			return;

		m_midiInEvents.push_back(_event);
	}

	void Je8086::readMidiOut(std::vector<synthLib::SMidiEvent>& _events)
	{
		m_midiOutParser.getEvents(m_midiOutEvents);

		if (_events.empty())
		{
			std::swap(_events, m_midiOutEvents);
		}
		else
		{
			_events.insert(_events.end(), m_midiOutEvents.begin(), m_midiOutEvents.end());
			m_midiOutEvents.clear();
		}
	}

	void Je8086::setEspEngine(const esp::Engine _engine)
	{
		if (m_rackAsics)
			m_rackAsics->setEspEngine(_engine);
		else
			m_keyboardAsics->setEspEngine(_engine);
	}

	void Je8086::runDsps()
	{
		const uint64_t dspCycles = m_machine.now() * 1323 / 625;
		const auto onFrame = [this](const int32_t _left, const int32_t _right) { onReceiveSample(_left, _right); };

		if (!m_rackAsics)
		{
			m_keyboardAsics->runForCycles(dspCycles, onFrame);
			return;
		}

		const auto onRackFrame = [this](const int32_t _left, const int32_t _right)
		{
			onReceiveSample(_left, _right);

			// the next frame's converter input
			const auto in = m_inputRead != m_inputWrite ? m_input[m_inputRead++ % m_input.size()] : SampleFrame{};
			auto& asic0 = m_rackAsics->first();
			asic0.writeGRAM(in.first, g_rackAdcLeft);
			asic0.writeGRAM(in.second, g_rackAdcRight);
		};

		// ASIC0 reports on an A/D channel what its DAC output currently is
		if (m_rackAsics->runForCycles(dspCycles, onRackFrame))
			m_faders.setFader(je8080::FaderAsic0EspDac, (m_rackAsics->first().readGRAM(0xe8) >> 6) & 1023);
	}

	void Je8086::setInput(const SampleFrame& _frame)
	{
		if (m_isRack)
			m_input[m_inputWrite++ % m_input.size()] = _frame;
	}

	void Je8086::step()
	{
		if (m_machine.now() > MidiInputHoldStates)
		{
			// Dense controller streams are thinned on the way in, see
			// ControllerIntervalSamples - the limiter itself drops nothing, so without
			// this a host sending faster than the wire backs up without bound and
			// delays every note behind it.
			for (auto& m : m_midiInEvents)
				m_controllerThinner.write(m, m_controllerThinnerClock);
			m_midiInEvents.clear();
		}

		// One sample period: 16 MHz / 44.1 kHz = 362 32/49 states.
		constexpr uint64_t sampleRate = 44100;
		m_stepRemainder += McuClock;
		const uint64_t states = m_stepRemainder / sampleRate;
		m_stepRemainder %= sampleRate;
		m_machine.run(states);
		runDsps();
	}

	Je8086::SampleFrame Je8086::renderSample()
	{
		if (m_sampleReadPos == m_sampleBuffer.size())
		{
			m_sampleBuffer.clear();
			m_sampleReadPos = 0;

			while (m_sampleBuffer.empty())
				step();
		}

		return m_sampleBuffer[m_sampleReadPos++];
	}

	void Je8086::onReceiveMidiByte(const uint8_t _byte)
	{
		if (_byte == 0xfe)	// active sensing stays out of the host event stream
			return;
		m_midiOutParser.write(_byte);
	}

	void Je8086::onReceiveSample(const int32_t _left, const int32_t _right)
	{
		// Hand over any controller value held back whose interval has passed, so it
		// reaches the limiter in time to be drained below.
		++m_controllerThinnerClock;
		m_controllerThinner.flush(m_controllerThinnerClock);

		m_midiInRateLimiter.processSample();
		m_sampleBuffer.emplace_back(_left, _right);
	}

	std::vector<uint8_t> Je8086::runFactoryReset()
	{
		uint64_t nextAction = FactoryResetScriptStart;
		int action = 0;

		while (true)
		{
			m_machine.run(4096);

			// a faulted CPU never advances, the script would wait forever
			if (m_machine.cpu().faulted())
				throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing, "The firmware faulted during the factory reset boot.");

			if (m_machine.now() < nextAction)
				continue;

			nextAction += FactoryResetScriptInterval;

			// keyboard: EXIT+7, VALUE UP to the confirmation, WRITE
			if (m_isRack)
			{
				switch (action++)
				{
				case 1: m_ports.releaseAll(); break;
				case 4: m_ports.press(50); break;
				case 5: m_ports.press(50, false); break;
				case 20: m_ports.press(61); break;
				case 21: m_ports.press(61, false); break;
				case 22: m_ports.press(51); break;
				case 23: m_ports.press(51, false); break;
				default:
					if (action > 6 && action < 19)
						m_ports.press(38, (action & 1) != 0);
					break;
				}
			}
			else
			{
				switch (action++)
				{
				case 1: m_ports.releaseAll(); break;
				case 4: m_ports.press(devices::kSwitch_Exit); break;
				case 5: m_ports.press(devices::kSwitch_7); break;
				case 6: m_ports.press(devices::kSwitch_7, false); m_ports.press(devices::kSwitch_Exit, false); break;
				case 40: m_ports.press(devices::kSwitch_ValueUp, false); m_ports.press(devices::kSwitch_Write); break;
				case 41: m_ports.press(devices::kSwitch_Write, false); break;
				default:
					if (action > 7 && action < 40)
						m_ports.press(devices::kSwitch_ValueUp, action & 1);
					break;
				}
			}

			if (action > 60)
			{
				std::vector<uint8_t> ram(RamSize);
				std::memcpy(ram.data(), m_machine.bus().ptr(0x200000), ram.size());
				return ram;
			}
		}
	}
}
