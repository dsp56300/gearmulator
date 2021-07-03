#pragma once

#include "midiTypes.h"

#include <functional>
#include <vector>

namespace virusLib
{
	class Resampler
	{
	public:
		using TMidiVec = std::vector<SMidiEvent>;
		using TProcessFunc = std::function<void(float**, TMidiVec&, uint32_t, uint32_t)>;

		Resampler(TProcessFunc _process, float _samplerateIn, float _samplerateOut);
		Resampler(const Resampler&) = delete;
		~Resampler();

		void process(float** _output, uint32_t _numChannels, uint32_t _numSamples, TMidiVec& _midiIn);

		float getSamplerateIn() const { return m_samplerateIn; }
		float getSamplerateOut() const { return m_samplerateOut; }

	private:
		uint32_t processResample(float** _output, uint32_t _numChannels, uint32_t _numSamples, TMidiVec& _midiIn);
		static void clearMidiEvents(TMidiVec& _midiEvents, int _usedLen);
		void destroyResamplers();
		void setChannelCount(uint32_t _numChannels);

		const float m_samplerateIn;
		const float m_samplerateOut;
		const float m_factorInToOut;
		const float m_factorOutToIn;

		std::vector<void*> m_resamplerOut;
		std::vector<SMidiEvent> m_midiEvents;

		std::vector< std::vector<float> > m_tempOutput;
		std::vector< float* > m_outputPtrs;
		std::vector<SMidiEvent> m_timeScaledMidiIn;

		TProcessFunc m_process;
	};
}
