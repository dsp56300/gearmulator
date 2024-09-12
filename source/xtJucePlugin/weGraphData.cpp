#include "weGraphData.h"

#include <complex>

namespace xtJucePlugin
{
	constexpr float g_pi = 3.1415926535f;

	constexpr auto g_size = std::tuple_size_v<xt::WaveData>;
	constexpr uint32_t g_fftOrder = 7;
	static_assert((1 << g_fftOrder) == g_size);

	GraphData::GraphData()  : m_source({}), m_data({}), m_frequencies({}), m_phases({}), m_fft(g_fftOrder)
	{
	}

	void GraphData::set(const xt::WaveData& _data)
	{
		if(_data == m_source)
			return;

		m_source = _data;

		for(uint32_t i=0; i<m_source.size(); ++i)
		{
//			m_data[i] = i >= (m_source.size()>>1) ? -1.0f : 1.0f;
//			m_data[i] = std::sin(static_cast<float>(i) / static_cast<float>(m_source.size()) * g_pi * 2.0f);
			m_data[i] = static_cast<float>(m_source[i]) / 128.0f;
		}

		updateFrequenciesAndPhases();

		onSourceChanged(m_source);
	}

	void GraphData::setData(const uint32_t _index, const float _value)
	{
		if(m_data[_index] == _value)
			return;
		m_data[_index] = _value;
		m_data[m_data.size() - _index - 1] = -_value;
		updateFrequenciesAndPhases();
		onSourceChanged(m_source);
	}

	void GraphData::setFreq(const uint32_t _index, const float _value)
	{
		if(m_frequencies[_index] == _value)
			return;
		m_frequencies[_index] = _value;
		updateDataFromFrequenciesAndPhases();
		onSourceChanged(m_source);
	}

	void GraphData::setPhase(const uint32_t _index, const float _value)
	{
		if(m_phases[_index] == _value)
			return;
		m_phases[_index] = _value;
		updateDataFromFrequenciesAndPhases();
		onSourceChanged(m_source);
	}

	void GraphData::updateFrequenciesAndPhases()
	{
		for(size_t i=0; i<m_data.size(); ++i)
			m_fftInData[i] = {m_data[i], 0.0f};

		m_fftOutData.fill({0.f,0.f});

		m_fft.perform(m_fftInData.data(), m_fftOutData.data(), false);

		const auto scale = 1.0f / static_cast<float>(m_fft.getSize()>>1);

		for(size_t i=0; i<m_frequencies.size(); ++i)
		{
			const auto re = m_fftOutData[i].real();
			const auto im = m_fftOutData[i].imag();

			std::complex<float> c(re,im);

			m_frequencies[i] = std::abs(c) * scale;
//			if(m_frequencies[i] > 0.01f)
				m_phases[i] = std::arg(c) / g_pi;
//			else
//				m_phases[i] = 0;
		}
	}

	void GraphData::updateDataFromFrequenciesAndPhases()
	{
		const auto scale = static_cast<float>(m_fft.getSize()>>1);

		for(uint32_t i=0; i<m_frequencies.size(); ++i)
		{
			const auto re = cos(m_phases[i] * g_pi) * scale * m_frequencies[i];
			const auto im = sin(m_phases[i] * g_pi) * scale * m_frequencies[i];

			m_fftInData[i].real(re);
			m_fftInData[i].imag(im);

			if(!i)
				continue;

			m_fftInData[128-i].real(re);
			m_fftInData[128-i].imag(-im);
		}

		m_fft.perform(m_fftInData.data(), m_fftOutData.data(), true);

		for(uint32_t i=0; i<m_data.size(); ++i)
		{
			m_data[i] = m_fftOutData[i].real();
		}
	}
}
