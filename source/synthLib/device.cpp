#include "device.h"

#include "audioTypes.h"
#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/memory.h"

using namespace dsp56k;

namespace synthLib
{
	Device::Device() = default;
	Device::~Device() = default;

	void Device::dummyProcess(const uint32_t _numSamples)
	{
		std::vector<float> buf;
		buf.resize(_numSamples);
		const auto ptr = &buf[0];

		TAudioInputs in = {ptr, ptr, nullptr, nullptr};//, nullptr, nullptr, nullptr, nullptr};
		TAudioOutputs out = {ptr, ptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

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
}
