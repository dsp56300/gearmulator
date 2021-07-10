#include "device.h"
#include "romfile.h"

namespace virusLib
{
	// 128k words beginning at $20000
	constexpr dsp56k::TWord g_externalMemStart	= 0x020000;
	constexpr dsp56k::TWord g_memorySize		= 0x040000;

	Device::Device(const std::string& _romFileName)
		: synthLib::Device(g_memorySize, g_externalMemStart)
		, m_rom(_romFileName)
		, m_syx(getHDI08(), m_rom)
	{
		auto loader = m_rom.bootDSP(getDSP(), getPeriph());

		startDSPThread();

		loader.join();

		dummyProcess(8);

		m_syx.sendInitControlCommands();

		dummyProcess(8);

		m_syx.send(Syx::PAGE_C, 0, Syx::PLAY_MODE, 2); // enable multi mode

		Syx::TPreset preset;
		
		// Send preset
//		m_rom.loadPreset(0, 93, preset);	// RepeaterJS
//		m_rom.loadPreset(0, 6, preset);		// BusysawsSV
//		m_rom.loadPreset(0, 12, preset);	// CommerseSV on Virus C
//		m_rom.loadPreset(0, 268, preset);	// CommerseSV on Virus B
//		m_rom.getSingle(0, 116, preset);	// Virus B: Choir 4 BC
		m_rom.getSingle(0, 0, preset);

		m_syx.sendSingle(0, 0, preset, false);
	}

	float Device::getSamplerate()
	{
		return 12000000.0f / 256.0f;
	}

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response)
	{
		if(_ev.sysex.empty())
		{
//			LOG("MIDI: " << std::hex << (int)me.a << " " << (int)me.b << " " << (int)me.c);
			return m_syx.sendMIDI(_ev.a, _ev.b, _ev.c, true);
		}

		synthLib::SMidiEvent response;

		if(!m_syx.sendSysex(_ev.sysex, true, response.sysex))
			return false;

		if(!response.sysex.empty())
			_response.emplace_back(response);

		return true;
	}

	void Device::readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		while(getHDI08().hasTX())
		{
			if(m_midiOutParser.append(getHDI08().readTX()))
			{
				const auto midi = m_midiOutParser.getMidiData();
				_midiOut.insert(_midiOut.end(), midi.begin(), midi.end());
				m_midiOutParser.clearMidiData();
			}
		}
	}
}
