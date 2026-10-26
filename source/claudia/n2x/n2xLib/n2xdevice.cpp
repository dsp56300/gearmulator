#include "n2xdevice.h"

#include "n2xhardware.h"
#include "n2xtypes.h"

namespace n2x
{
	Device::Device(const synthLib::DeviceCreateParams& _params)
		: DspClockDevice(_params)
		, m_hardware(_params.romData, _params.romName)
		, m_state(&m_hardware, &getMidiTranslator())
	{
	}

	float Device::getSamplerate() const
	{
		return g_samplerate;
	}

	bool Device::isValid() const
	{
		return m_hardware.isValid();
	}

	bool Device::getState(std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		return m_state.getState(_state);
	}

	bool Device::setState(const std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		return m_state.setState(_state);
	}

	uint32_t Device::getChannelCountIn()
	{
		return 0;
	}

	uint32_t Device::getChannelCountOut()
	{
		return 4;
	}

	void Device::getDspClocks(std::vector<dsp56k::EsxiClock*>& _clocks) const
	{
		auto& hw = const_cast<Hardware&>(m_hardware);
		_clocks.push_back(&hw.getDSPA().getPeriph().getEsaiClock());
		_clocks.push_back(&hw.getDSPB().getPeriph().getEsaiClock());
	}

	void Device::readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		m_hardware.getMidi().read(m_midiOutBytes);
		parseMidiOutBytes(_midiOut);

		for (const auto& midiOut : _midiOut)
		{
			if(midiOut.sysex.empty())
				LOG("TX " << HEXN(midiOut.a,2) << ' ' << HEXN(midiOut.b,2) << ' ' << HEXN(midiOut.c, 2));
			else
				LOG("TX Sysex of size " << midiOut.sysex.size());
			m_state.receive(midiOut);
		}

		// Arp Hold is not part of any dump and the firmware does not send it, the editor learns about it from here
		const auto arpHold = m_state.getArpHoldMask();
		if(arpHold != m_arpHoldMask)
		{
			m_arpHoldMask = arpHold;
			_midiOut.emplace_back(synthLib::MidiEventSource::Internal);
			_midiOut.back().sysex = State::createArpHoldSysex(arpHold);
		}
	}

	void Device::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples)
	{
		m_hardware.processAudio(_outputs, static_cast<uint32_t>(_samples), getExtraLatencySamples());
	}

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response)
	{
		if(_ev.sysex.empty())
		{
			// drop program change messages. We do not have any valid presets in the device, this will select garbage
			if((_ev.a & 0xf0) == synthLib::M_PROGRAMCHANGE)
				return true;

			m_state.receive(_response, _ev);
			auto e = _ev;
			stampOffset(e);
			m_hardware.sendMidi(e);
		}
		else
		{
			if(m_state.receive(_response, _ev))
				return true;

			// the firmware answers a single request a while later
			if (auto matcher = State::createReplyMatcher(_ev.sysex))
				expectReply(_ev.source, std::move(matcher));

			m_hardware.sendMidi(_ev);
		}
		return true;
	}
}
