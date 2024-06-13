#include "weData.h"

#include "xtController.h"

namespace xtJucePlugin
{
	WaveEditorData::WaveEditorData(Controller& _controller) : m_controller(_controller)
	{
	}

	void WaveEditorData::requestData()
	{
		if(isWaitingForWave())
			return;

		for(uint32_t i=0; i<m_romWaves.size(); ++i)
		{
			if(!m_romWaves[i])
			{
				requestWave(i);
				return;
			}
		}

		for(uint32_t i=0; i<m_ramWaves.size(); ++i)
		{
			if(!m_ramWaves[i])
			{
				requestWave(i + xt::Wave::g_firstRamWaveIndex);
				return;
			}
		}
	}

	void WaveEditorData::onReceiveWave(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg)
	{
		const auto hh = _data.at(pluginLib::MidiDataType::Bank);
		const auto ll = _data.at(pluginLib::MidiDataType::Program);

		const uint32_t index = (hh << 7) | ll;

		if(!xt::Wave::isValidWaveIndex(index))
			return;

		/*
			mw2_sysex.pdf:

			"A Wave consists of 128 eight Bit samples, but only the first 64 of them are
			stored/transmitted, the second half is same as first except the values are
			negated and the order is reversed:

			Wave[64+n] = -Wave[63-n] for n=0..63

			Note that samples are not two's complement format, to get a signed byte,
			the most significant bit must be flipped:

			signed char s = Wave[n] ^ 0x80"
		*/

		WaveData data;

		constexpr auto off = 7;

		for(uint32_t i=0; i<data.size()>>1; ++i)
		{
			auto sample = (_msg[off + i]) << 4 | _msg[off + i + 1];
			sample = sample ^ 0x80;

			data[i] = static_cast<int8_t>(sample);
			data[64+i] = static_cast<int8_t>(-sample);
		}

		setWave(index, data);

		if(m_currentWaveRequestIndex == index)
		{
			m_currentWaveRequestIndex = InvalidWaveIndex;
			requestData();
		}
	}

	bool WaveEditorData::requestWave(const uint32_t _index)
	{
		if(isWaitingForWave())
			return false;

		if(!m_controller.requestWave(_index))
			return false;
		m_currentWaveRequestIndex = _index;
		return true;
	}

	bool WaveEditorData::setWave(uint32_t _index, const WaveData& _data)
	{
		if(_index < m_romWaves.size())
		{
			m_romWaves[_index] = _data;
			return true;
		}
		if(_index < xt::Wave::g_firstRamWaveIndex)
			return false;

		_index -= xt::Wave::g_firstRamWaveIndex;

		if(_index >= m_ramWaves.size())
			return false;

		m_ramWaves[_index] = _data;
		return true;
	}
}
