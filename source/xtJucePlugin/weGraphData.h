#pragma once

#include "weTypes.h"
#include <tuple>

#include "jucePluginLib/event.h"

#include "juce_dsp/juce_dsp.h"

namespace xtJucePlugin
{
	class GraphData
	{
	public:
		pluginLib::Event<WaveData> onSourceChanged;

		GraphData();

		void set(const WaveData& _data);

		const auto& getData() const { return m_data; }
		const auto& getFrequencies() const { return m_frequencies; }
		const auto& getPhases() const { return m_phases; }

		void setData(uint32_t _index, float _value);
		void setFreq(uint32_t _index, float _value);
		void setPhase(uint32_t _index, float _value);

	private:
		void updateFrequenciesAndPhases();
		void updateDataFromFrequenciesAndPhases();

		WaveData m_source;

		std::array<float, std::tuple_size_v<WaveData>> m_data;
		std::array<float, std::tuple_size_v<WaveData>/2> m_frequencies;
		std::array<float, std::tuple_size_v<WaveData>/2> m_phases;

		std::array<juce::dsp::Complex<float>, std::tuple_size_v<WaveData>> m_fftInData; 
		std::array<juce::dsp::Complex<float>, std::tuple_size_v<WaveData>> m_fftOutData;

		const juce::dsp::FFT m_fft;
	};
}
