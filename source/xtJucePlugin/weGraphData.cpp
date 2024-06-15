#include "weGraphData.h"

#include "juce_dsp/juce_dsp.h"

#include <complex>

namespace xtJucePlugin
{
	constexpr float g_pi = 3.1415926535f;

	void GraphData::set(const WaveData& _data)
	{
		if(_data == m_source)
			return;

		m_source = _data;

		constexpr auto size = std::tuple_size_v<WaveData>;
		constexpr uint32_t order = 7;
		static_assert((1 << order) == size);

		const juce::dsp::FFT fft(order);

		for(uint32_t i=0; i<m_source.size(); ++i)
		{
//			m_data[i] = i >= (m_source.size()>>1) ? -1.0f : 1.0f;
//			m_data[i] = std::sin(static_cast<float>(i) / static_cast<float>(m_source.size()) * g_pi * 2.0f);
			m_data[i] = static_cast<float>(m_source[i]) / 128.0f;
		}

		std::array<juce::dsp::Complex<float>, size> inData; 
		std::array<juce::dsp::Complex<float>, size> outData;

		for(size_t i=0; i<m_data.size(); ++i)
			inData[i] = {m_data[i], 0.0f};

		outData.fill({0.f,0.f});

		fft.perform(inData.data(), outData.data(), false);

		for(size_t i=0; i<m_frequencies.size(); ++i)
		{
			const auto re = outData[i].real() / static_cast<float>(fft.getSize()>>1);
			const auto im = outData[i].imag() / static_cast<float>(fft.getSize()>>1);

			std::complex c(re,im);

			m_frequencies[i] = std::abs(c);
//			if(m_frequencies[i] > 0.01f)
				m_phases[i] = std::arg(c) / g_pi;
//			else
//				m_phases[i] = 0;
		}

		onSourceChanged(m_source);
	}
}
