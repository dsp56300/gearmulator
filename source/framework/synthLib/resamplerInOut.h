#pragma once

#include "audiobuffer.h"
#include "midiTypes.h"
#include "resampler.h"

#include <memory>	// unique_ptr

namespace synthLib
{
	class ResamplerInOut
	{
	public:
		using TMidiVec = std::vector<SMidiEvent>;
		using TProcessFunc = std::function<void(const TAudioInputs&, const TAudioOutputs&, size_t, const TMidiVec&, TMidiVec&)>;

		ResamplerInOut(uint32_t _channelCountIn, uint32_t _channelCountOut);

		void setResamplerMode(Resampler::Mode _mode);
		void setDeviceSamplerate(float _samplerate);
		void setHostSamplerate(float _samplerate);
		void setSamplerates(float _hostSamplerate, float _deviceSamplerate);
		void prepareDeviceSamplerates(const std::vector<float>& _samplerates);

		void process(const TAudioInputs& _inputs, TAudioOutputs& _outputs, const TMidiVec& _midiIn, TMidiVec& _midiOut, uint32_t _numSamples, const TProcessFunc& _processFunc);

		uint32_t getOutputLatency() const { return m_outputLatency; }
		uint32_t getInputLatency() const { return m_inputLatency; }

	private:
		void recreate();
		void prepareAlternatives();
		void swapStream(ResamplerInOut& _other);
		void clearAudioHistory();
		void rescaleQueuedMidi(float _newDeviceSamplerate);
		std::vector<float> m_dynamicSamplerates;
		std::vector<std::unique_ptr<ResamplerInOut>> m_alternatives;
		// Appends _src to _dst with every offset scaled. scaleMidiEvents() replaces _dst instead.
		static void appendScaledMidiEvents(TMidiVec& _dst, const TMidiVec& _src, float _scale);
		static void scaleMidiEvents(TMidiVec& _dst, const TMidiVec& _src, float _scale);
		static void clampMidiEvents(TMidiVec& _dst, const TMidiVec& _src, uint32_t _offsetMin, uint32_t _offsetMax);
		static void extractMidiEvents(TMidiVec& _dst, const TMidiVec& _src, uint32_t _offsetMin, uint32_t _offsetMax);

		// --- Members are in two groups. swapStream() hands the live stream over to a cached
		// alternative when the device rate changes, so every member below that describes *where the
		// stream currently is* has to move with it, and every member that describes *what this
		// object is for* must stay put. There is no way to check that at compile time here - the
		// class holds a pmr vector and pointers, so its size differs per target - so the grouping
		// is the check: add a member to the right block and swapStream() reads as obviously
		// complete or obviously not.

		// Not swapped: identity and configuration. Both objects are built for the same host rate,
		// channel counts and conversion mode, and the alternatives belong to the live object only.
		const uint32_t m_channelCountIn;
		const uint32_t m_channelCountOut;
		float m_samplerateHost = 0;
		Resampler::Mode m_mode = Resampler::Mode::Legacy;

		// Swapped by swapStream(): the converters and the audio in flight. Keep this block and
		// swapStream() in step.
		std::unique_ptr<Resampler> m_out = nullptr;
		std::unique_ptr<Resampler> m_in = nullptr;

		float m_samplerateDevice = 0;

		AudioBuffer m_scaledInput;
		AudioBuffer m_input;

		size_t m_scaledInputSize = 0;

		TMidiVec m_processedMidiIn;

		// Queued input belongs to the live stream, never to a cached rate, so this one stays put
		// while everything around it is handed over. It used to be swapped here and swapped back by
		// the caller one line earlier - deleting either half handed the live queue to a resampler
		// nothing is reading.
		TMidiVec m_midiIn;

		TMidiVec m_midiOut;

		uint32_t m_inputLatency = 0;
		uint32_t m_outputLatency = 0;
	};
}
