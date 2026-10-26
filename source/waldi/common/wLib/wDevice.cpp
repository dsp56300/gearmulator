#include "wDevice.h"

#include "wMidiTypes.h"

namespace wLib
{
	namespace
	{
		// commands 00 to 0f ask for a dump, which has the command plus 10
		constexpr uint8_t g_dumpCommand = 0x10;
		constexpr uint8_t g_multiRequest = 0x01;

		bool isDumpRequest(const SysEx& _sysex)
		{
			return _sysex.size() > IdxCommand + 1 && _sysex.front() == 0xf0 && _sysex.back() == 0xf7 &&
				_sysex[IdxIdWaldorf] == IdWaldorf && _sysex[IdxCommand] < g_dumpCommand;
		}
	}

	Device::Device(const synthLib::DeviceCreateParams& _params) : DspClockDevice(_params)
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
		if (m_frontPanel.receive(_ev.sysex))
			return true;

		Responses responses;

		const auto res = stateReceive(responses, _ev, State::Origin::External);

		for (auto& response : responses)
		{
			auto& r = _response.emplace_back(synthLib::MidiEventSource::Device);
			std::swap(response, r.sysex);
		}

		// the cache could not answer, the firmware does
		if (responses.empty() && isDumpRequest(_ev.sysex))
			expectDump(_ev.source, _ev.sysex);

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

	void Device::expectDump(const synthLib::MidiEventSource _requester, const SysEx& _request)
	{
		const auto command = _request[IdxCommand];

		// the dump has the buffer and location of the request, if it names them. There is one multi edit buffer, the
		// location in its dump is not always that of the request.
		// ponytail: a request for a whole bank (buffer 10) gets dumps with the bank's own buffer, which match nothing and
		// go out as before. Only a librarian on a port asks for one, the editor never does
		std::optional<uint8_t> buffer;
		std::optional<uint8_t> location;

		if (_request.size() > IdxBuffer + 1)
			buffer = _request[IdxBuffer];
		if (_request.size() > IdxLocation + 1 && command != g_multiRequest)
			location = _request[IdxLocation];

		expectReply(_requester, synthLib::expectBytes(getDumpHeader(_request[IdxIdMachine], static_cast<uint8_t>(command + g_dumpCommand), buffer, location)));
	}
}
