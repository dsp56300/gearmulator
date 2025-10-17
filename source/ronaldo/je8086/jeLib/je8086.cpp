#include "je8086.h"

#include "baseLib/filesystem.h"

#include "synthLib/deviceException.h"

namespace jeLib
{
	Je8086::Je8086(const std::vector<uint8_t>& _romData, const std::string& _ramDataFilename)
	: ports([this](devices::Port* _port) { onLedsChanged(_port); })
	, midi(0, [this](uint8_t _byte) { onReceiveMidiByte(_byte); })
	, m_midiOutParser(synthLib::MidiEventSource::Device)
	, m_midiInRateLimiter([this](const uint8_t _byte) { midi.provideMIDI(&_byte, 1); })
	{
		if (_romData.empty())
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing, "ROM data is empty");

		std::vector<unsigned char> ram;
		baseLib::filesystem::readFile(ram, _ramDataFilename);

		m_factoryreset = ram.size() != 256 * 1024;

		emu.loadmem(_romData.data(),(int)_romData.size(), 0);

		if (!m_factoryreset) 
			emu.loadmem(ram.data(), static_cast<int>(ram.size()), 0x200000);

		emu.memmap(&catchall, (int)_romData.size(), 0x200000 - (int)_romData.size());	// access to gap between flash and sram
		emu.memmap(&catchall, 0x240000, 0xFFFD10 - 0x240000);	// access to gap between end of SRAM and start of on-chip ram
		emu.memmap(&catchall, 0xFFFF10, 0xFFFF1C - 0xFFFF10);	// access to gap between end of on-chip ram and hw registers
		emu.memmap(&catchall, 0x410000, 0x600000 - 0x410000);	// access to gap between asic and lcd/keys
		emu.memmap(&asics, 0x400000, 0x10000);
		emu.memmap(&keyscanner, 0x600000, 4);
		emu.memmap(&lcd, 0x600004, 2);
		emu.memmap(&hwregs, 0xffff1c, 0xe4);
		emu.memmap(&faders, 0xffffe0, 9);	// ADC controls
		emu.memmap(&faders, 0xffffcb, 1);	// port 6
		emu.memmap(&timers, 0xffff60, 64);	// timers
		emu.memmap(&ports, 0xffffd3, 2);
		emu.memmap(&ports, 0xffffd6, 1);
		emu.memmap(&midi, 0xffffb0, 6);
		emu.boot();

		if (m_factoryreset)
		{
			runfactoryreset(_ramDataFilename); // Run a factory reset if needs be.
			return;
		}

		asics.setPostSample([this](const int32_t _left, const int32_t _right) { onReceiveSample(_left, _right); });

		m_midiInRateLimiter.setSamplerate(88200.0f);
		m_midiInRateLimiter.setDefaultRateLimit();
		m_midiInRateLimiter.setSysexPause(0.021f);	// according to manual 20ms pause between sysex patch messages
		m_midiInRateLimiter.setSysexPauseLengthThreshold(100);

		lcd.setChangeCallback([this] { onLcdDdRamChanged(); });
		lcd.setCgRamChangeCallback([this] { onLcdCgRamChanged(); });
	}

	void Je8086::addMidiEvent(const synthLib::SMidiEvent& _event)
	{
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

	void Je8086::step()
	{
		uint64_t now = emu.getCycles();

		if (now > 12776184)// && !((++ctr) & 0x3fff))
		{
			for (auto& m : m_midiInEvents)
				m_midiInRateLimiter.write(std::move(m));
			m_midiInEvents.clear();
		}

		emu.step();
		timers.tick();
		midi.tick();

		asics.runForCycles(emu.getCycles() * 1323 / 625); // Convert from uC cycles to DSP steps. (this is (clockrate / 2) / (uc clock = 16000000), simplified)
	}

	void Je8086::onLedsChanged(devices::Port*/* _port*/)
	{
		/*
		for(uint32_t i=0; i<66; ++i) 
			_port->getLed(i);
		*/
	}

	void Je8086::onReceiveMidiByte(uint8_t _byte)
	{
		m_midiOutParser.write(_byte);
	}

	void Je8086::onReceiveSample(int32_t _left, int32_t _right)
	{
		m_midiInRateLimiter.processSample();
		m_sampleBuffer.emplace_back(_left, _right);
	}

	void Je8086::onLcdDdRamChanged()
	{
		SysexRemoteControl::sendSysexLcdDdRam(m_midiOutEvents, lcd);
	}

	void Je8086::onLcdCgRamChanged()
	{
		SysexRemoteControl::sendSysexLcdCgRam(m_midiOutEvents, lcd);
	}

	void Je8086::runfactoryreset(const std::string& _ramDataFilename)
	{
		using namespace devices;

		int ctr = 0, ctr2 = 0;

		while (true)
		{
			emu.step();
			timers.tick();

			if (emu.getCycles() > 112776184 && !((++ctr) & 0x3ffff))
			{
				switch (ctr2++)
				{
					case 1: ports.releaseAll();	break;
					case 4: ports.press(kSwitch_Exit);	break;
					case 5: ports.press(kSwitch_7);	break;
					case 6: ports.press(kSwitch_7,0); ports.press(kSwitch_Exit,0);	break;
					case 40: ports.press(kSwitch_ValueUp, 0); ports.press(kSwitch_Write); break;
					case 41: ports.press(kSwitch_Write,0); break;
					default:
						if (ctr2 > 7 && ctr2 < 40) ports.press(devices::kSwitch_ValueUp, ctr2 & 1);
						if (ctr2 > 60)
						{
							uint8 ram[0x40000];

							emu.readMemory(ram, 0x200000, 0x40000);

							baseLib::filesystem::writeFile(_ramDataFilename, ram, std::size(ram));
							printf("Saved repaired RAM to disk. Rebooting.\n");
							emu.boot();
							return;
						}
				}
			}
		}
	}
}
