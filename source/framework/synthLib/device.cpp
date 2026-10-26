#include "device.h"

#include "audioTypes.h"

#include "dsp56kBase/logging.h"

#include <algorithm>
#include <cmath>

namespace synthLib
{
	namespace
	{
		// The time a firmware may take to start its answer or to send its next part. A JP-8000 answers an RQ1 about a
		// second after a state load, with a DT1 per block 130 to 210 ms apart
		constexpr float g_replyTimeoutSeconds = 3.0f;
	}

	Device::Device(const DeviceCreateParams& _params) : m_createParams(_params)  // NOLINT(modernize-pass-by-value) dll transition, do not mess with the input data
	{
	}
	Device::~Device() = default;

	const std::vector<SMidiEvent>& Device::bridgeProcess(const TAudioInputs& _inputs, const TAudioOutputs& _outputs, const size_t _size, const std::vector<SMidiEvent>& _midiIn)
	{
		process(_inputs, _outputs, _size, _midiIn, m_bridgeMidiOut);
		return m_bridgeMidiOut;
	}

	const std::vector<uint8_t>& Device::bridgeGetState(const StateType _type)
	{
		m_bridgeState.clear();
		getState(m_bridgeState, _type);
		return m_bridgeState;
	}

	const std::vector<float>& Device::bridgeGetSupportedSamplerates()
	{
		m_bridgeSupportedSamplerates.clear();
		getSupportedSamplerates(m_bridgeSupportedSamplerates);
		return m_bridgeSupportedSamplerates;
	}

	const std::vector<float>& Device::bridgeGetPreferredSamplerates()
	{
		m_bridgePreferredSamplerates.clear();
		getPreferredSamplerates(m_bridgePreferredSamplerates);
		return m_bridgePreferredSamplerates;
	}

	void Device::dummyProcess(const uint32_t _numSamples)
	{
		std::vector<float> buf;
		buf.resize(_numSamples);
		const auto ptr = buf.data();

		const TAudioInputs in = {ptr, ptr, nullptr, nullptr};//, nullptr, nullptr, nullptr, nullptr};
		const TAudioOutputs out = {ptr, ptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

		std::vector<SMidiEvent> midi;

		process(in, out, _numSamples, midi, midi);
	}

	void Device::process(const TAudioInputs& _inputs, const TAudioOutputs& _outputs, const size_t _size, const std::vector<SMidiEvent>& _midiIn, std::vector<SMidiEvent>& _midiOut)
	{
		_midiOut.clear();

		for (const auto& ev : arpeggiate(_midiIn, _size))
		{
			if (ev.type == MidiEventType::TransportDiscontinuity)
			{
				onTransportDiscontinuity(ev);
				continue;
			}

			if (ev.type == MidiEventType::HostTransport)
			{
				onHostTransport(ev);
				continue;
			}

			m_translatorOut.clear();

			m_midiTranslator.process(m_translatorOut, ev);

			for(auto & e : m_translatorOut)
			{
				if (e.sysex.empty())
				{
					sendMidi(e, _midiOut);
					continue;
				}

				// requests are SysEx, what a device answers to one right away is its reply
				const auto first = _midiOut.size();
				sendMidi(e, _midiOut);
				markReplies(_midiOut, first, e.source);
			}
		}

		processAudio(_inputs, _outputs, _size);

		readMidiOut(_midiOut);

		markExpectedReplies(_midiOut, _size);

		m_numSamplesProcessed += static_cast<uint32_t>(_size);
	}

	void Device::parseMidiOutBytes(std::vector<SMidiEvent>& _midiOut)
	{
		m_midiOutParser.write(m_midiOutBytes);
		m_midiOutBytes.clear();
		m_midiOutParser.getEvents(_midiOut);
	}

	void Device::expectReply(const MidiEventSource _requester, ReplyMatcher _matcher)
	{
		m_expectedReplies.push_back({_requester, std::move(_matcher), getReplyTimeoutSamples()});
	}

	void Device::markExpectedReplies(std::vector<SMidiEvent>& _midiOut, const size_t _size)
	{
		if (m_expectedReplies.empty())
			return;

		for (auto& e : _midiOut)
		{
			// what the device answered in C++ is a reply already
			if (e.sysex.empty() || e.replyTo != MidiEventSource::Unknown)
				continue;

			for (auto it = m_expectedReplies.begin(); it != m_expectedReplies.end(); ++it)
			{
				const auto match = it->matcher(e);

				if (match == ReplyMatch::None)
					continue;

				e.replyTo = it->requester;

				if (match == ReplyMatch::Complete)
					m_expectedReplies.erase(it);
				else
					it->samplesLeft = getReplyTimeoutSamples();
				break;
			}
		}

		const auto samples = static_cast<uint32_t>(_size);

		m_expectedReplies.erase(std::remove_if(m_expectedReplies.begin(), m_expectedReplies.end(), [samples](ExpectedReply& _r)
		{
			if (_r.samplesLeft <= samples)
				return true;
			_r.samplesLeft -= samples;
			return false;
		}), m_expectedReplies.end());
	}

	uint32_t Device::getReplyTimeoutSamples() const
	{
		return static_cast<uint32_t>(getSamplerate() * g_replyTimeoutSeconds);
	}

	const std::vector<SMidiEvent>& Device::arpeggiate(const std::vector<SMidiEvent>& _midiIn, const size_t _size)
	{
		if (!hasArpeggiators())
			return _midiIn;

		m_arpMidi.assign(_midiIn.begin(), _midiIn.end());
		m_arpOut.clear();

		processArpeggiators(m_arpMidi, m_arpOut, static_cast<uint32_t>(_size));

		// What the arpeggiators play goes in by its offset, after what is there at the same offset already
		for (const auto& ev : m_arpOut)
		{
			auto it = m_arpMidi.end();

			if (!m_arpMidi.empty() && m_arpMidi.back().offset > ev.offset)
			{
				it = m_arpMidi.begin();
				while (it != m_arpMidi.end() && it->offset <= ev.offset)
					++it;
			}

			m_arpMidi.insert(it, ev);
		}

		return m_arpMidi;
	}

	void Device::setExtraLatencySamples(const uint32_t _size)
	{
		constexpr uint32_t maxLatency = 16384;  // must match Audio::RingBufferSize / 2 in dsp56kEmu

		m_extraLatency = std::min(_size, maxLatency);

		LOG("Latency set to " << m_extraLatency << " samples at " << getSamplerate() << " Hz");

		if(_size > maxLatency)
		{
			LOG("Warning, limited requested latency " << _size << " to maximum value " << maxLatency << ", audio will be out of sync!");
		}
	}

	bool Device::isSamplerateSupported(const float& _samplerate) const
	{
		std::vector<float> srs;
		getSupportedSamplerates(srs);
		for (const auto& sr : srs)
		{
			if(std::fabs(sr - _samplerate) < 1.0f)
				return true;
		}
		return false;
	}

	bool Device::setSamplerate(const float _samplerate)
	{
		return isSamplerateSupported(_samplerate);
	}

	float Device::getDeviceSamplerate(const float _preferredDeviceSamplerate, const float _hostSamplerate) const
	{
		if(_preferredDeviceSamplerate > 0.0f && isSamplerateSupported(_preferredDeviceSamplerate))
			return _preferredDeviceSamplerate;
		return getDeviceSamplerateForHostSamplerate(_hostSamplerate);
	}

	float Device::getDeviceSamplerateForHostSamplerate(const float _hostSamplerate) const
	{
		std::vector<float> preferred;
		getPreferredSamplerates(preferred);

		// if there is no choice we need to use the only one that is supported
		if(preferred.size() == 1)
			return preferred.front();

		// find the lowest possible samplerate that is higher than the host samplerate
		const std::set<float> samplerates(preferred.begin(), preferred.end());

		for (const float sr : preferred)
		{
			if(sr >= _hostSamplerate)
				return sr;
		}

		// if all supported device samplerates are lower than the host samplerate, use the maximum that the device supports
		return *samplerates.rbegin();
	}
}
