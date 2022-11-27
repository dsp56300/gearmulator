#include "device.h"

#include "dspMultiTI.h"
#include "dspSingleSnow.h"
#include "romfile.h"

namespace virusLib
{
	Device::Device(const ROMFile& _rom, const bool _createDebugger/* = false*/)
		: synthLib::Device()
		, m_rom(_rom)
	{
		if(!m_rom.isValid())
			return;

		DspSingle* dsp1;
		createDspInstances(dsp1, m_dsp2, m_rom);
		m_dsp.reset(dsp1);

		m_dsp->getPeriphX().getEsai().setCallback([this](dsp56k::Audio*)
		{
			onAudioWritten();
		}, 0);

		m_mc.reset(new Microcontroller(m_dsp->getHDI08(), _rom));

		if(m_dsp2)
			m_mc->addHDI08(m_dsp2->getHDI08());

		auto loader = bootDSP(*m_dsp, m_rom, _createDebugger);

		if(m_dsp2)
		{
			auto loader2 = bootDSP(*m_dsp2, m_rom, false);
			loader2.join();
		}

		loader.join();

		while(!m_mc->dspHasBooted())
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

	bool Device::getState(std::vector<uint8_t>& _state, const synthLib::StateType _type)
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

	uint32_t Device::getChannelCountIn()
	{
		return 2;
	}

	uint32_t Device::getChannelCountOut()
	{
		return m_rom.isTIFamily() ? 12 : 6;
	}

	void Device::createDspInstances(DspSingle*& _dspA, DspSingle*& _dspB, const ROMFile& _rom)
	{
#if VIRUS_SUPPORT_TI
		if(_rom.getModel() == ROMFile::Model::Snow)
		{
			_dspA = new DspSingleSnow();
		}
		else if(_rom.getModel() == ROMFile::Model::TI)
		{
			auto* dsp = new DspMultiTI();
			_dspA = dsp;
			_dspB = &dsp->getDSP2();
		}
		else
#endif
		{
			_dspA = new DspSingle(_rom.isTIFamily() ? 0x100000 : 0x040000, _rom.isTIFamily());
		}

		configureDSP(*_dspA, _rom);

		if(_dspB)
			configureDSP(*_dspB, _rom);
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
		m_mc->processHdi08Tx(_midiOut);
	}

	void Device::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples)
	{
		constexpr auto maxBlockSize = dsp56k::Audio::RingBufferSize>>2;

		auto inputs(_inputs);
		auto outputs(_outputs);

		while(_samples > maxBlockSize)
		{
			m_dsp->processAudio(inputs, outputs, maxBlockSize, getExtraLatencySamples());

			_samples -= maxBlockSize;

			for (auto& input : inputs)
			{
				if(input)
					input += maxBlockSize;
			}

			for (auto& output : outputs)
			{
				if(output)
					output += maxBlockSize;
			}
		}

		m_dsp->processAudio(inputs, outputs, _samples, getExtraLatencySamples());
	}

	void Device::onAudioWritten()
	{
		m_mc->process(1);

		m_numSamplesWritten += 1;

		m_mc->sendPendingMidiEvents(m_numSamplesWritten >> 1);
	}

	void Device::configureDSP(DspSingle& _dsp, const ROMFile& _rom)
	{
		auto& jit = _dsp.getJIT();
		auto conf = jit.getConfig();

		if(_rom.isTIFamily())
		{
			auto& clock = _dsp.getPeriphX().getEsaiClock();

			if(_rom.getModel() == ROMFile::Model::Snow)
			{
				clock.setExternalClockFrequency(44100 * 256);
			}
			else
			{
//				clock.setSamplerate(static_cast<int>(getSamplerate()));
				clock.setExternalClockFrequency(44100 * 256);
				clock.setSamplerate(44100 * 3);
				clock.setEsaiDivider(&_dsp.getPeriphY().getEsai(), 0);
				clock.setEsaiDivider(&_dsp.getPeriphX().getEsai(), 2);
			}

			conf.aguSupportBitreverse = true;
		}
		else
		{
			conf.aguSupportBitreverse = false;
		}

		jit.setConfig(conf);
	}

	std::thread Device::bootDSP(DspSingle& _dsp, const ROMFile& _rom, const bool _createDebugger)
	{
		auto res = _rom.bootDSP(_dsp.getDSP(), _dsp.getPeriphX());
		_dsp.startDSPThread(_createDebugger);
		return res;
	}
}
