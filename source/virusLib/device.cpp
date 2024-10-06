#include "device.h"

#include "dspMultiTI.h"
#include "dspSingleSnow.h"
#include "romfile.h"

#include "dsp56kEmu/jit.h"

#include "synthLib/deviceException.h"
#include "synthLib/midiToSysex.h"

#include <cstring>

#include "dspMemoryPatches.h"

namespace virusLib
{
	Device::Device(ROMFile _rom, const float _preferredDeviceSamplerate, const float _hostSamplerate, const bool _createDebugger/* = false*/)
		: m_rom(std::move(_rom))
		, m_samplerate(getDeviceSamplerate(_preferredDeviceSamplerate, _hostSamplerate))
	{
		m_frontpanelStateMidiEvent.source = synthLib::MidiEventSource::Internal;

		DspSingle* dsp1;
		createDspInstances(dsp1, m_dsp2, m_rom, m_samplerate);
		m_dsp.reset(dsp1);

		m_dsp->getAudio().setCallback([this](dsp56k::Audio*)
		{
			onAudioWritten();
		}, 0);

		m_mc.reset(new Microcontroller(*m_dsp, m_rom, false));

		if(m_dsp2)
			m_mc->addDSP(*m_dsp2, true);

		bootDSPs(m_dsp.get(), m_dsp2, m_rom, _createDebugger);

//		m_dsp->getMemory().saveAssembly("P.asm", 0, m_dsp->getMemory().sizeP(), true, false, m_dsp->getDSP().getPeriph(0), m_dsp->getDSP().getPeriph(1));

		switch(m_rom.getModel())
		{
		case DeviceModel::A:
			// The A does not send any event to notify that it has finished booting
			dummyProcess(32);
			m_dsp->disableESSI1();
			break;
		case DeviceModel::B:
			// Rack Classic doesn't send that it has finished booting either, wait a bit for the event but abort if it takes too long
			{
				constexpr auto maxRetries = 256;
				uint32_t r=0;
				while(!m_mc->dspHasBooted() && ++r <= maxRetries)
					dummyProcess(8);
				if(r >= maxRetries)
					LOG("Timed out while waiting for the device to finish booting, expecting that it has booted");
			}
			break;
		default:
			while(!m_mc->dspHasBooted())
				dummyProcess(8);
		}

		m_mc->sendInitControlCommands();

		dummyProcess(8);

		m_mc->createDefaultState();
	}

	Device::~Device()
	{
		m_dsp->getAudio().setCallback(nullptr,0);
		m_mc.reset();
		m_dsp.reset();
	}

	std::vector<float> Device::getSupportedSamplerates() const
	{
		switch (m_rom.getModel())
		{
		default:
		case DeviceModel::A:
		case DeviceModel::B:
		case DeviceModel::C:
			return {12000000.0f / 256.0f};
		case DeviceModel::Snow:
		case DeviceModel::TI:
		case DeviceModel::TI2:
			return {32000.0f, 44100.0f, 48000.0f, 64000.0f, 88200.0f, 96000.0f};
		}
	}

	std::vector<float> Device::getPreferredSamplerates() const
	{
		switch (m_rom.getModel())
		{
		default:
		case DeviceModel::ABC:
			return getSupportedSamplerates();
		case DeviceModel::Snow:
		case DeviceModel::TI:
		case DeviceModel::TI2:
			return {44100.0f, 48000.0f};
		}
	}

	float Device::getSamplerate() const
	{
		return m_samplerate;
	}

	bool Device::setSamplerate(const float _samplerate)
	{
		if(!synthLib::Device::setSamplerate(_samplerate))
			return false;
		m_samplerate = _samplerate;
		configureDSP(*m_dsp, m_rom, m_samplerate);
		if(m_dsp2)
			configureDSP(*m_dsp2, m_rom, m_samplerate);
		m_mc->setSamplerate(_samplerate);
		return true;
	}

	bool Device::isValid() const
	{
		return m_rom.isValid();
	}

	void Device::process(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _size, const std::vector<synthLib::SMidiEvent>& _midiIn, std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		m_frontpanelStateDSP.clear();

		synthLib::Device::process(_inputs, _outputs, _size, _midiIn, _midiOut);

		if(m_rom.isTIFamily())
		{
			// Apparently this LED model did not want a completely closed PWM
			constexpr auto minimumValue = 0.785339653f;
			constexpr auto maximumValue = 0.999146f;

			if(m_rom.getModel() == DeviceModel::Snow)
			{
				m_frontpanelStateDSP.m_lfoPhases[0] = 1.f;
				m_frontpanelStateDSP.m_lfoPhases[1] = 1.f;
				m_frontpanelStateDSP.m_lfoPhases[2] = 1.f;
				m_frontpanelStateDSP.m_logo = 1.f;

				FrontpanelState::updatePhaseFromTimer(m_frontpanelStateDSP.m_bpm, m_dsp->getDSP(), 1, 0.83f, 1.0f);
			}
			else
			{
				m_frontpanelStateDSP.updateLfoPhaseFromTimer(m_dsp->getDSP(), 0, 1, minimumValue, maximumValue);
				m_frontpanelStateDSP.updateLfoPhaseFromTimer(m_dsp->getDSP(), 1, 2, minimumValue, maximumValue);
				m_frontpanelStateDSP.updateLfoPhaseFromTimer(m_dsp->getDSP(), 2, 0, minimumValue, maximumValue);

				FrontpanelState::updatePhaseFromTimer(m_frontpanelStateDSP.m_logo, m_dsp2->getDSP(), 0, 0.963654f, 1);
			}
		}
		else
		{
			m_frontpanelStateDSP.updateLfoPhaseFromTimer(m_dsp->getDSP(), 0, 2);	// TIMER 1 = ACI = LFO 1 LED
			m_frontpanelStateDSP.updateLfoPhaseFromTimer(m_dsp->getDSP(), 1, 1);	// TIMER 2 = ADO = LFO 2/3 LED
		}

		m_numSamplesProcessed += static_cast<uint32_t>(_size);

		m_frontpanelStateDSP.toMidiEvent(m_frontpanelStateMidiEvent);
		_midiOut.push_back(m_frontpanelStateMidiEvent);
	}

#if !SYNTHLIB_DEMO_MODE
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

		if(parseTIcontrolPreset(messages, _state))
			return m_mc->setState(messages);

		std::vector<std::vector<uint8_t>> sysexMessages;
		synthLib::MidiToSysex::splitMultipleSysex(sysexMessages, _state, false);

		if(sysexMessages.empty())
			return false;

		for (const auto& sysexMessage : sysexMessages)
			messages.emplace_back().sysex = sysexMessage;

		return m_mc->setState(messages);
	}
#endif

	bool Device::find4CC(uint32_t& _offset, const std::vector<uint8_t>& _data, const std::string_view& _4cc)
	{
		if(_data.size() < (_offset + _4cc.size()))
			return false;

		for(uint32_t i=_offset; i<_data.size() - _4cc.size(); ++i)
		{
			bool valid = true;
			for(size_t j=0; j<_4cc.size(); ++j)
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

		uint32_t numFound = 0;

		while(readPos < _state.size() - 4)
		{
			if(!find4CC(readPos, _state, "MIDI"))
				break;

			if(readPos >= _state.size())
				break;

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
				break;

			const auto controllerAssignmentsLen = nextLen();

			readPos += controllerAssignmentsLen;
			
			while(readPos < _state.size())
			{
				const auto midiDataLen = nextLen();

				if(!midiDataLen)
					break;

				if((readPos + midiDataLen) > _state.size())
					break;

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

				if(!e.sysex.empty())
					++numFound;
			}			
		}

		return numFound > 0;
	}

	bool Device::parsePowercorePreset(std::vector<std::vector<uint8_t>>& _sysexPresets, const std::vector<uint8_t>& _data)
	{
		uint32_t off = 0;

		uint32_t numFound = 0;

		while(off < _data.size() - 4)
		{
			// VST2 fxp/fxb chunk must exist
			if(!find4CC(off, _data, "CcnK"))
				break;

			off += 4;

			uint32_t pos;

			// fxp or fxb?
			if(find4CC(off, _data, "FPCh"))
				pos = off + 0x34;					// fxp
			else if(find4CC(off, _data, "FBCh"))
				pos = off + 0x98;					// fxb
			else
				continue;

			if(pos >= _data.size())
				break;

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

				++numFound;

				++programIndex;
				pos += presetSize;
				pos += padding;
			}
			off = pos;
		}

		return numFound > 0;
	}

	bool Device::parseVTIBackup(std::vector<std::vector<uint8_t>>& _sysexPresets, const std::vector<uint8_t>& _data)
	{
		if(_data.size() < 512)
			return false;

		// first 11 bytes are the serial number. Check if they're all ASCII
		for(size_t i=0; i<11; ++i)
		{
			if(_data[i] < 32 || _data[i] > 127)
				return false;
		}

		constexpr size_t presetSize = sizeof(Microcontroller::TPreset);
		Microcontroller::TPreset preset;

		constexpr uint32_t maxPresets = (4 + 26) * 128;	// 4x RAM banks, 26x ROM banks, 128 patches per bank

		uint32_t presetIdx = 0;

		// presets start at $20
		// They are "raw" presets, i.e. 512 bytes of preset data each
		// The sysex packaging is missing, i.e. the single dump header, the checksums and the sysex terminator
		for(size_t i=0x20; i<_data.size() - presetSize; i += presetSize)
		{
			memcpy(preset.data(), &_data[i], presetSize);

			const auto name = ROMFile::getSingleName(preset);

			if(name.size() != 10)
				break;

			auto& sysex = _sysexPresets.emplace_back(std::vector<uint8_t>{
				0xf0, 0x00, 0x20, 0x33, 0x01, OMNI_DEVICE_ID, DUMP_SINGLE,
				static_cast<uint8_t>((presetIdx >> 7) & 0x7f),
				static_cast<uint8_t>(presetIdx & 0x7f)});

			sysex.reserve(9 + 256 + 1 + 256 + 2);	// header, 256 preset bytes, 1st checksum, 256 preset bytes, 2nd checksum, EOX

			for(size_t j=0; j<256; ++j)
				sysex.push_back(preset[j]);
			sysex.push_back(Microcontroller::calcChecksum(sysex, 5));
			for(size_t j=256; j<512; ++j)
				sysex.push_back(preset[j]);
			sysex.push_back(Microcontroller::calcChecksum(sysex, 5));
			sysex.push_back(0xf7);

			++presetIdx;

			if(presetIdx == maxPresets)
				break;
		}

		return true;
	}

	uint32_t Device::getInternalLatencyMidiToOutput() const
	{
		// Note that this is an average value, midi latency drifts in a range of roughly +/- 61 samples
		constexpr auto latency = 324;

		if(m_rom.isTIFamily())
			return latency - 108;	// TI seems to have improved a bit

		return latency;	
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

	void Device::createDspInstances(DspSingle*& _dspA, DspSingle*& _dspB, const ROMFile& _rom, const float _samplerate)
	{
		if(_rom.getModel() == DeviceModel::Snow)
		{
			_dspA = new DspSingleSnow();
		}
		else if(_rom.getModel() == DeviceModel::TI || _rom.getModel() == DeviceModel::TI2)
		{
			auto* dsp = new DspMultiTI();
			_dspA = dsp;
			_dspB = &dsp->getDSP2();
		}
		else
		{
			_dspA = new DspSingle(_rom.isTIFamily() ? 0x100000 : 0x040000, _rom.isTIFamily(), nullptr, _rom.getModel() == DeviceModel::A);
		}

		configureDSP(*_dspA, _rom, _samplerate);

		if(_dspB)
			configureDSP(*_dspB, _rom, _samplerate);
	}

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response)
	{
		if(_ev.sysex.empty())
		{
//			LOG("MIDI: " << std::hex << (int)_ev.a << " " << (int)_ev.b << " " << (int)_ev.c);
			auto ev = _ev;
			ev.offset += m_numSamplesProcessed + getExtraLatencySamples();
			return m_mc->sendMIDI(ev, &m_frontpanelStateDSP);
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
		m_mc->process();
	}

	void Device::configureDSP(DspSingle& _dsp, const ROMFile& _rom, const float _samplerate)
	{
		auto& jit = _dsp.getJIT();
		auto conf = jit.getConfig();

		if(_rom.isTIFamily())
		{
			auto& clock = _dsp.getPeriphX().getEsaiClock();

			const auto sr = static_cast<int>(_samplerate);

			clock.setExternalClockFrequency(std::min(sr, 48000) * 256);

			if(_rom.getModel() != DeviceModel::Snow)
			{
				clock.setSamplerate(sr * 3);
				clock.setEsaiDivider(&_dsp.getPeriphY().getEsai(), 0);
				clock.setEsaiDivider(&_dsp.getPeriphX().getEsai(), 2);
			}

			conf.aguSupportBitreverse = true;
			conf.maxDoIterations = 32;

			clock.setClockSource(dsp56k::EsaiClock::ClockSource::Cycles);
		}
		else
		{
			conf.aguSupportBitreverse = false;
		}

		jit.setConfig(conf);
	}

	std::thread Device::bootDSP(DspSingle& _dsp, const ROMFile& _rom, const bool _createDebugger)
	{
		auto res = _rom.bootDSP(_dsp);
		_dsp.startDSPThread(_createDebugger);
		return res;
	}

	void Device::bootDSPs(DspSingle* _dspA, DspSingle* _dspB, const ROMFile& _rom, bool _createDebugger)
	{
		auto loader = bootDSP(*_dspA, _rom, _createDebugger);

		if(_dspB)
		{
			auto loader2 = bootDSP(*_dspB, _rom, false);
			loader2.join();
		}

		loader.join();

//		applyDspMemoryPatches(_dspA, _dspB, _rom);
	}

	bool Device::setDspClockPercent(const uint32_t _percent)
	{
		if(!m_dsp)
			return false;

		bool res = m_dsp->getEsxiClock().setSpeedPercent(_percent);

		if(m_dsp2)
			res &= m_dsp2->getEsxiClock().setSpeedPercent(_percent);

		return res;
	}

	uint32_t Device::getDspClockPercent() const
	{
		return !m_dsp ? 0 : m_dsp->getEsxiClock().getSpeedPercent();
	}

	uint64_t Device::getDspClockHz() const
	{
		return !m_dsp ? 0 : m_dsp->getEsxiClock().getSpeedInHz();
	}

	void Device::applyDspMemoryPatches(const DspSingle* _dspA, const DspSingle* _dspB, const ROMFile& _rom)
	{
		DspMemoryPatches::apply(_dspA, _rom.getHash());
		DspMemoryPatches::apply(_dspB, _rom.getHash());
	}

	void Device::applyDspMemoryPatches() const
	{
		applyDspMemoryPatches(m_dsp.get(), m_dsp2, m_rom);
	}
}
