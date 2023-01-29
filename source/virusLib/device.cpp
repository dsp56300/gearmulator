#include "device.h"

#include "dspSingle.h"
#include "romfile.h"

#include "dsp56kEmu/jit.h"

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

		m_mc.reset(new Microcontroller(*m_dsp, _rom, false));

		if(m_dsp2)
			m_mc->addDSP(*m_dsp2, true);

		auto loader = bootDSP(*m_dsp, m_rom, _createDebugger);

		if(m_dsp2)
		{
			auto loader2 = bootDSP(*m_dsp2, m_rom, false);
			loader2.join();
		}

		loader.join();

//		m_dsp->getMemory().saveAssembly("P.asm", 0, m_dsp->getMemory().sizeP(), true, false, m_dsp->getDSP().getPeriph(0), m_dsp->getDSP().getPeriph(1));

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

	bool Device::setStateFromUnknownCustomData(const std::vector<uint8_t>& _state)
	{
		std::vector<synthLib::SMidiEvent> messages;
		if(!parseTIcontrolPreset(messages, _state))
			return false;
		return m_mc->setState(messages);
	}

	bool Device::find4CC(uint32_t& _offset, const std::vector<uint8_t>& _data, const std::string& _4cc)
	{
		for(uint32_t i=0; i<_data.size() - _4cc.size(); ++i)
		{
			bool valid = true;
			for(size_t j=0; j<4; ++j)
			{
				if(static_cast<char>(_data[i + j]) == _4cc[j])
					continue;
				valid = false;
				break;
			}
			if(valid)
			{
				_offset = i;
				return true;
			}
		}
		return false;
	}

	bool Device::parseTIcontrolPreset(std::vector<synthLib::SMidiEvent>& _events, const std::vector<uint8_t>& _state)
	{
		if(_state.size() < 8)
			return false;

		uint32_t readPos = 0;

		if(!find4CC(readPos, _state, "MIDI"))
			return false;

		if(readPos >= _state.size())
			return false;

		auto readLen = [&_state](const size_t _offset) -> uint32_t
		{
			if(_offset + 4 > _state.size())
				return 0;
			const uint32_t o =
				(static_cast<uint32_t>(_state[_offset+0]) << 24) | 
				(static_cast<uint32_t>(_state[_offset+1]) << 16) |
				(static_cast<uint32_t>(_state[_offset+2]) << 8) |
				(static_cast<uint32_t>(_state[_offset+3]));
			return o;
		};

		auto nextLen = [&readPos, &readLen]() -> uint32_t
		{
			const auto len = readLen(readPos);
			readPos += 4;
			return len;
		};

		const auto dataLen = nextLen();

		if(dataLen + readPos > _state.size())
			return false;

		const auto controllerAssignmentsLen = nextLen();

		readPos += controllerAssignmentsLen;
		
		while(readPos < _state.size())
		{
			const auto midiDataLen = nextLen();

			if(!midiDataLen)
				break;

			if((readPos + midiDataLen) > _state.size())
				return false;

			synthLib::SMidiEvent& e = _events.emplace_back();

			e.sysex.assign(_state.begin() + readPos, _state.begin() + readPos + midiDataLen);

			if(e.sysex.front() != 0xf0)
			{
				assert(e.sysex.size() <= 3);
				e.a = e.sysex[0];
				if(e.sysex.size() > 1)
					e.b = e.sysex[1];
				if(e.sysex.size() > 2)
					e.c = e.sysex[2];

				e.sysex.clear();
			}

			readPos += midiDataLen;
		}

		return true;
	}

	bool Device::parsePowercorePreset(std::vector<std::vector<uint8_t>>& _sysexPresets, const std::vector<uint8_t>& _data)
	{
		uint32_t off = 0;

		// VST2 fxp/fxb chunk must exist
		if(!find4CC(off, _data, "CcnK"))
			return false;

		uint32_t pos = 0;

		// fxp or fxb?
		if(find4CC(off, _data, "FPCh"))
			pos = off + 0x34;					// fxp
		else if(find4CC(off, _data, "FBCh"))
			pos = off + 0x98;					// fxb
		else
			return false;

		if(pos >= _data.size())
			return false;

		++pos;	// skip first byte, version?

		constexpr uint32_t presetSize = 256;			// presets seem to be stored without sysex packaging
		constexpr uint32_t padding = 5;					// five unknown bytes betweeen two presets

		uint8_t programIndex = 0;

		while((pos + presetSize) <= static_cast<uint32_t>(_data.size()))
		{
			Microcontroller::TPreset p;
			memcpy(&p.front(), &_data[pos], presetSize);

			const auto version = Microcontroller::getPresetVersion(p);
			if(version != C)
				break;
			const auto name = ROMFile::getSingleName(p);
			if(name.size() != 10)
				break;

			// pack into sysex
			std::vector<uint8_t>& sysex = _sysexPresets.emplace_back(std::vector<uint8_t>{0xf0, 0x00, 0x20, 0x33, 0x01, OMNI_DEVICE_ID, 0x10, 0x01, programIndex});
			sysex.insert(sysex.end(), _data.begin() + pos, _data.begin() + pos + presetSize);
			sysex.push_back(Microcontroller::calcChecksum(sysex, 5));
			sysex.push_back(0xf7);

			++programIndex;
			pos += presetSize;
			pos += padding;
		}

		return !_sysexPresets.empty();
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
		return 6;
	}

	void Device::createDspInstances(DspSingle*& _dspA, DspSingle*& _dspB, const ROMFile& _rom)
	{
		_dspA = new DspSingle(0x040000, false);

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
		m_mc->readMidiOut(_midiOut);
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
		m_mc->getMidiQueue(0).onAudioWritten();
		m_mc->process(1);
	}

	void Device::configureDSP(DspSingle& _dsp, const ROMFile& _rom)
	{
		auto& jit = _dsp.getJIT();
		auto conf = jit.getConfig();

		conf.aguSupportBitreverse = false;
		conf.aguSupportMultipleWrapModulo = false;
		conf.dynamicPeripheralAddressing = false;

		jit.setConfig(conf);
	}

	std::thread Device::bootDSP(DspSingle& _dsp, const ROMFile& _rom, const bool _createDebugger)
	{
		auto res = _rom.bootDSP(_dsp.getDSP(), _dsp.getPeriphX());
		_dsp.startDSPThread(_createDebugger);
		return res;
	}
}
