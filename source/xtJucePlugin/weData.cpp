#include "weData.h"

#include "xtController.h"
#include "dsp56kEmu/logging.h"

namespace xtJucePlugin
{
	WaveEditorData::WaveEditorData(Controller& _controller) : m_controller(_controller)
	{
	}

	void WaveEditorData::requestData()
	{
		if(isWaitingForData())
			return;

		for(uint32_t i=0; i<m_tables.size(); ++i)
		{
			if(!m_tables[i] && !isAlgorithmicTable(i))
			{
				LOG("Request table " << i);
				requestTable(i);
				return;
			}
		}

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
		const auto index = toIndex(_data);

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
			auto sample = (_msg[off + (i<<1)]) << 4 | _msg[off + (i<<1) + 1];
			sample = sample ^ 0x80;

			data[i] = static_cast<int8_t>(sample);
			data[127-i] = static_cast<int8_t>(-sample);
		}

		setWave(index, data);

		if(m_currentWaveRequestIndex == index)
		{
			m_currentWaveRequestIndex = InvalidWaveIndex;
			requestData();
		}
	}

	void WaveEditorData::onReceiveTable(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg)
	{
		const auto index = toIndex(_data);

		if(!xt::Wave::isValidTableIndex(index))
			return;

		constexpr uint32_t off = 7;

		TableData table;

		for(uint32_t i=0; i<64; ++i)
		{
			const auto i4 = i<<2;

			auto waveIdx = _msg[i4+off] << 12;
			waveIdx |= _msg[i4+off+1] << 8;
			waveIdx |= _msg[i4+off+2] << 4;
			waveIdx |= _msg[i4+off+3];

			table[i] = static_cast<uint16_t>(waveIdx);
		}

		setTable(index, table);

		if(m_currentTableRequestIndex == index)
		{
			m_currentTableRequestIndex = InvalidWaveIndex;
			requestData();
		}
	}

	std::optional<WaveData> WaveEditorData::getWave(uint32_t _waveIndex) const
	{
		if(_waveIndex < m_romWaves.size())
			return m_romWaves[_waveIndex];

		if(_waveIndex < xt::Wave::g_firstRamWaveIndex)
			return {};
		_waveIndex -= xt::Wave::g_firstRamWaveIndex;
		if(_waveIndex >= m_ramWaves.size())
			return {};

		if(!m_ramWaves[_waveIndex])
			return {};

		return m_ramWaves[_waveIndex];
	}

	uint32_t WaveEditorData::getWaveIndex(uint32_t _tableIndex, uint32_t _indexInTable) const
	{
		if(_tableIndex >= m_tables.size())
			return InvalidWaveIndex;
		if(_indexInTable >= std::tuple_size<TableData>())
			return InvalidWaveIndex;
		const auto table = m_tables[_tableIndex];
		if(!table)
			return InvalidWaveIndex;
		return (*table)[_indexInTable];
	}

	std::optional<WaveData> WaveEditorData::getWave(const uint32_t _tableIndex, const uint32_t _indexInTable) const
	{
		return getWave(getWaveIndex(_tableIndex, _indexInTable));
	}

	uint32_t WaveEditorData::toIndex(const pluginLib::MidiPacket::Data& _data)
	{
		const auto hh = _data.at(pluginLib::MidiDataType::Bank);
		const auto ll = _data.at(pluginLib::MidiDataType::Program);

		return (hh << 7) | ll;
	}

	bool WaveEditorData::requestWave(const uint32_t _index)
	{
		if(isWaitingForData())
			return false;

		if(!m_controller.requestWave(_index))
			return false;
		m_currentWaveRequestIndex = _index;
		return true;
	}

	bool WaveEditorData::requestTable(const uint32_t _index)
	{
		if(isWaitingForData())
			return false;

		if(!m_controller.requestTable(_index))
			return false;
		m_currentTableRequestIndex = _index;
		return true;
	}

	bool WaveEditorData::setWave(uint32_t _index, const WaveData& _data)
	{
		if(_index < m_romWaves.size())
		{
			m_romWaves[_index] = _data;
			onWaveChanged(_index);
			return true;
		}
		if(_index < xt::Wave::g_firstRamWaveIndex)
			return false;

		_index -= xt::Wave::g_firstRamWaveIndex;

		if(_index >= m_ramWaves.size())
			return false;

		m_ramWaves[_index] = _data;
		onWaveChanged(_index + xt::Wave::g_firstRamWaveIndex);
		return true;
	}

	bool WaveEditorData::setTable(uint32_t _index, const TableData& _data)
	{
		if(_index >= m_tables.size())
			return false;

		m_tables[_index] = _data;
		onTableChanged(_index);
		return true;
	}

	bool WaveEditorData::isAlgorithmicTable(const uint32_t _index)
	{
		for (const uint32_t i : xt::Wave::g_algorithmicWavetables)
		{
			if(_index == i)
				return true;
		}
		return false;
	}
}
