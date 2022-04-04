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
		using TProcessFunc = std::function<void(float**, float**, size_t, const TMidiVec&, TMidiVec&)>;

		ResamplerInOut() = default;

		void setDeviceSamplerate(float _samplerate);
		void setHostSamplerate(float _samplerate);

		void process(float** _inputs, float** _outputs, const TMidiVec& _midiIn, TMidiVec& _midiOut, uint32_t _numSamples, const TProcessFunc& _processFunc);

		uint32_t getOutputLatency() const { return m_outputLatency; }
		uint32_t getInputLatency() const { return m_inputLatency; }

	private:
		void recreate();
		static void scaleMidiEvents(TMidiVec& _dst, const TMidiVec& _src, float _scale);
		static void clampMidiEvents(TMidiVec& _dst, const TMidiVec& _src, uint32_t _offsetMin, uint32_t _offsetMax);
		static void extractMidiEvents(TMidiVec& _dst, const TMidiVec& _src, uint32_t _offsetMin, uint32_t _offsetMax);

		std::unique_ptr<Resampler> m_out = nullptr;
		std::unique_ptr<Resampler> m_in = nullptr;

		float m_samplerateDevice = 0;
		float m_samplerateHost = 0;

		AudioBuffer m_scaledInput;
		AudioBuffer m_input;
		AudioBuffer m_scaledOutput;
		AudioBuffer m_output;

		size_t m_scaledInputSize = 0;

		TMidiVec m_processedMidiIn;

		TMidiVec m_midiIn;
		TMidiVec m_midiOut;

		uint32_t m_inputLatency = 0;
		uint32_t m_outputLatency = 0;
	};
}
