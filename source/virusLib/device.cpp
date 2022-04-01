#include "device.h"
#include "romfile.h"

namespace virusLib
{
	constexpr dsp56k::TWord g_externalMemStart	= 0x020000;

	Device::Device(const ROMFile& _romFile)
		: synthLib::Device(_romFile.isTIFamily() ? 0x100000 : 0x040000, g_externalMemStart)
		, m_rom(_romFile)
		, m_syx(getHDI08(), m_rom)
	{
		if(!m_rom.isValid())
			return;

		auto& jit = getDSP().getJit();
		auto conf = jit.getConfig();

		if(m_rom.isTIFamily())
		{
			getPeriphX().getEsai().setSamplerate(static_cast<int>(getSamplerate()));
			getPeriphY().getEsai().setSamplerate(static_cast<int>(getSamplerate()));

			conf.aguSupportBitreverse = true;
		}
		else
		{
			conf.aguSupportBitreverse = false;
		}

		jit.setConfig(conf);

		auto loader = m_rom.bootDSP(getDSP(), getPeriphX());

		startDSPThread();

		loader.join();

		dummyProcess(8);

		m_syx.sendInitControlCommands();

		dummyProcess(8);

		m_syx.createDefaultState();
	}

	float Device::getSamplerate() const
	{
		if (m_rom.isTIFamily())
			return 44100.0f;

		return 12000000.0f / 256.0f;
	}

	bool Device::isValid() const
	{
		return m_rom.isValid();
	}

	void Device::process(float** _inputs, float** _outputs, size_t _size, const std::vector<synthLib::SMidiEvent>& _midiIn, std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		synthLib::Device::process(_inputs, _outputs, _size, _midiIn, _midiOut);
		m_syx.process(_size);

		m_numSamplesProcessed += static_cast<uint32_t>(_size);
	}

	bool Device::getState(std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		return m_syx.getState(_state, _type);
	}

	bool Device::setState(const std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		return m_syx.setState(_state, _type);
	}

	uint32_t Device::getInternalLatencySamples() const
	{
		return 300;		// hard to belive but this is what I figured out by measuring with the init patch
	}

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response)
	{
		if(_ev.sysex.empty())
		{
//			LOG("MIDI: " << std::hex << (int)_ev.a << " " << (int)_ev.b << " " << (int)_ev.c);
			auto ev = _ev;
			ev.offset += m_numSamplesProcessed + getLatencySamples();
			return m_syx.sendMIDI(ev, true);
		}

		std::vector<synthLib::SMidiEvent> responses;

		if(!m_syx.sendSysex(_ev.sysex, true, responses, _ev.source))
			return false;

		for (const auto& response : responses)
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

	void Device::onAudioWritten()
	{
		m_numSamplesWritten += 1;

		m_syx.sendPendingMidiEvents(m_numSamplesWritten >> 1);
	}
}
