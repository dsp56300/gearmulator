#include "weData.h"

#include "xtController.h"
#include "dsp56kEmu/logging.h"
#include "xtLib/xtState.h"

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

		xt::WaveData data;
		xt::State::parseWaveData(data, _msg);

		setWave(index, data);

		if(m_currentWaveRequestIndex == index)
		{
			m_currentWaveRequestIndex = g_invalidWaveIndex;
			requestData();
		}
	}

	void WaveEditorData::onReceiveTable(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg)
	{
		const auto index = toIndex(_data);

		if(!xt::Wave::isValidTableIndex(index))
			return;

		xt::TableData table;

		xt::State::parseTableData(table, _msg);

		setTable(index, table);

		if(m_currentTableRequestIndex == index)
		{
			m_currentTableRequestIndex = g_invalidWaveIndex;
			requestData();
		}
	}

	std::optional<xt::WaveData> WaveEditorData::getWave(uint32_t _waveIndex) const
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
			return g_invalidWaveIndex;
		if(_indexInTable >= std::tuple_size<xt::TableData>())
			return g_invalidWaveIndex;
		const auto table = m_tables[_tableIndex];
		if(!table)
			return g_invalidWaveIndex;
		return (*table)[_indexInTable];
	}

	std::optional<xt::TableData> WaveEditorData::getTable(uint32_t _tableIndex) const
	{
		if(_tableIndex >= m_tables.size())
			return {};
		return m_tables[_tableIndex];
	}

	bool WaveEditorData::swapTableEntries(uint32_t _table, uint32_t _indexA, uint32_t _indexB)
	{
		if(_indexA == _indexB)
			return false;
		if(_table >= m_tables.size())
			return false;
		const auto& table = m_tables[_table];
		if(!table)
			return false;
		auto t = *table;
		std::swap(t[_indexA], t[_indexB]);
		m_tables[_table] = t;
		onTableChanged(_table);
		return true;
	}

	bool WaveEditorData::setTableWave(uint32_t _table, uint32_t _index, uint32_t _waveIndex)
	{
		if(_table >= m_tables.size())
			return false;
		const auto& table = m_tables[_table];
		if(!table)
			return false;
		auto t = *table;
		if(_index >= t.size())
			return false;
		if(t[_index] == _waveIndex)
			return false;
		t[_index] = static_cast<uint16_t>(_waveIndex);
		m_tables[_table] = t;
		onTableChanged(_table);
		return true;
	}

	std::optional<xt::WaveData> WaveEditorData::getWave(const uint32_t _tableIndex, const uint32_t _indexInTable) const
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

	bool WaveEditorData::setWave(uint32_t _index, const xt::WaveData& _data)
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

	bool WaveEditorData::setTable(uint32_t _index, const xt::TableData& _data)
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
