#include "device.h"
#include "romfile.h"

namespace virusLib
{
	constexpr dsp56k::TWord g_externalMemStart	= 0x020000;

	Device::Device(const ROMFile& _romFile)
		: synthLib::Device()
		, m_rom(_romFile)
	{
		if(!m_rom.isValid())
			return;

		m_dsp.reset(new DspSingle(_romFile.isTIFamily() ? 0x100000 : 0x040000, g_externalMemStart, _romFile.isTIFamily()));

		m_dsp->getPeriphX().getEsai().setCallback([this](dsp56k::Audio*)
		{
			onAudioWritten();
		}, 0);

		auto& jit = m_dsp->getJIT();
		auto conf = jit.getConfig();

		if(m_rom.isTIFamily())
		{
			auto& clock = m_dsp->getPeriphX().getEsaiClock();

			if(m_rom.getModel() == ROMFile::Model::Snow)
			{
				clock.setExternalClockFrequency(44100 * 256);
			}
			else
			{
//				clock.setSamplerate(static_cast<int>(getSamplerate()));
				clock.setExternalClockFrequency(44100 * 256);
				clock.setSamplerate(44100 * 3);
				clock.setEsaiDivider(&m_dsp->getPeriphY().getEsai(), 0);
				clock.setEsaiDivider(&m_dsp->getPeriphX().getEsai(), 2);
			}

			conf.aguSupportBitreverse = true;
		}
		else
		{
			conf.aguSupportBitreverse = false;
		}

		jit.setConfig(conf);

		m_mc.reset(new Microcontroller(m_dsp->getHDI08(), _romFile));

		auto loader = m_rom.bootDSP(m_dsp->getDSP(), m_dsp->getPeriphX());

		m_dsp->startDSPThread();

		loader.join();

		dummyProcess(8);

		m_mc->sendInitControlCommands();

		dummyProcess(8);

		m_mc->createDefaultState();
	}

	Device::~Device()
	{
		m_dsp->getPeriphX().getEsai().setCallback(nullptr,0);
		m_mc.reset();
		m_dsp.reset();
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

	void Device::process(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _size, const std::vector<synthLib::SMidiEvent>& _midiIn, std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		synthLib::Device::process(_inputs, _outputs, _size, _midiIn, _midiOut);

		m_numSamplesProcessed += static_cast<uint32_t>(_size);
	}

	bool Device::getState(std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		return m_mc->getState(_state, _type);
	}

	bool Device::setState(const std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		return m_mc->setState(_state, _type);
	}

	uint32_t Device::getInternalLatencyMidiToOutput() const
	{
		// Note that this is an average value, midi latency drifts in a range of roughly +/- 61 samples
		return 324;
	}

	uint32_t Device::getInternalLatencyInputToOutput() const
	{
		// Measured by using an input init patch. Sent a click to the input and recorded both the input
		// as direct signal plus the Virus output and checking the resulting latency in a wave editor
		return 384;
	}

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response)
	{
		if(_ev.sysex.empty())
		{
//			LOG("MIDI: " << std::hex << (int)_ev.a << " " << (int)_ev.b << " " << (int)_ev.c);
			auto ev = _ev;
			ev.offset += m_numSamplesProcessed + getExtraLatencySamples();
			return m_mc->sendMIDI(ev);
		}

		std::vector<synthLib::SMidiEvent> responses;

		if(!m_mc->sendSysex(_ev.sysex, responses, _ev.source))
			return false;

		for (const auto& response : responses)
			_response.emplace_back(response);

		return true;
	}

	void Device::readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		while(m_dsp->getHDI08().hasTX())
		{
			if(m_midiOutParser.append(m_dsp->getHDI08().readTX()))
			{
				const auto midi = m_midiOutParser.getMidiData();
				_midiOut.insert(_midiOut.end(), midi.begin(), midi.end());
				m_midiOutParser.clearMidiData();
			}
		}
	}

	void Device::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, const size_t _samples)
	{
		m_dsp->processAudio(_inputs, _outputs, _samples, getExtraLatencySamples());
	}

	void Device::onAudioWritten()
	{
		m_mc->process(1);

		m_numSamplesWritten += 1;

		m_mc->sendPendingMidiEvents(m_numSamplesWritten >> 1);
	}
}
