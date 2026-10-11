#include "wDevice.h"

#include "wSysexRemoteControl.h"

namespace wLib
{
	Device::Device(const synthLib::DeviceCreateParams& _params, SysexRemoteControl& _sysexRemote)
		: DspClockDevice(_params)
		, m_sysexRemote(_sysexRemote)
	{
	}

	void Device::getDspClocks(std::vector<dsp56k::EsxiClock*>& _clocks) const
	{
		if(auto* c = getDspEsxiClock())
			_clocks.push_back(c);
	}

	void Device::readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		receiveMidiBytes(m_midiOutBytes);
		parseMidiOutBytes(_midiOut);

		// By index up to the current end: the responses are appended to the same vector, which can reallocate
		Responses responses;

		const auto count = _midiOut.size();

		for (size_t i = 0; i < count; ++i)
		{
			responses.clear();
			stateReceive(responses, _midiOut[i], State::Origin::Device);

			for (auto& response : responses)
			{
				auto& r = _midiOut.emplace_back(synthLib::MidiEventSource::Device);
				std::swap(response, r.sysex);
			}
		}

		if(!m_customSysexOut.empty())
		{
			_midiOut.insert(_midiOut.begin(), m_customSysexOut.begin(), m_customSysexOut.end());
			m_customSysexOut.clear();
		}
	}

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response)
	{
		if (!_ev.sysex.empty() && m_sysexRemote.receive(m_customSysexOut, _ev.sysex))
			return true;

		Responses responses;

		const auto res = stateReceive(responses, _ev, State::Origin::External);

		for (auto& response : responses)
		{
			auto& r = _response.emplace_back(synthLib::MidiEventSource::Device);
			std::swap(response, r.sysex);
		}

		// do not forward to device if our cache was able to reply. It might have sent something to the device already on its own if a cache miss occured
		if(res)
			return true;

		if(_ev.sysex.empty())
		{
			auto e = _ev;
			stampOffset(e);
			sendMidiToSynth(e);
		}
		else
		{
			sendMidiToSynth(_ev);
		}

		return true;
	}
}
