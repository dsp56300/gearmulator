#include "device.h"

#include "audioTypes.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"

#include <cmath>

using namespace dsp56k;

namespace synthLib
{
	Device::Device() = default;
	Device::~Device() = default;

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
		for (const auto& ev : _midiIn)
			sendMidi(ev, _midiOut);

		processAudio(_inputs, _outputs, _size);

		readMidiOut(_midiOut);
	}

	void Device::setExtraLatencySamples(const uint32_t _size)
	{
		constexpr auto maxLatency = Audio::RingBufferSize >> 1;

		m_extraLatency = std::min(_size, maxLatency);

		LOG("Latency set to " << m_extraLatency << " samples at " << getSamplerate() << " Hz");

		if(_size > maxLatency)
		{
			LOG("Warning, limited requested latency " << _size << " to maximum value " << maxLatency << ", audio will be out of sync!");
		}
	}

	bool Device::isSamplerateSupported(const float& _samplerate) const
	{
		for (const auto& sr : getSupportedSamplerates())
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
		const auto preferred = getPreferredSamplerates();

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
