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

		for(uint16_t i=0; i<static_cast<uint16_t>(m_tables.size()); ++i)
		{
			const auto id = xt::TableId(i);
			if(!m_tables[i] && !isAlgorithmicTable(id))
			{
				LOG("Request table " << i);
				requestTable(id);
				return;
			}
		}

		for(uint16_t i=0; i<static_cast<uint16_t>(m_romWaves.size()); ++i)
		{
			const auto id = xt::WaveId(i);
			if(!m_romWaves[i])
			{
				requestWave(id);
				return;
			}
		}

		for(uint32_t i=0; i<m_ramWaves.size(); ++i)
		{
			if(!m_ramWaves[i])
			{
				requestWave(xt::WaveId(static_cast<uint16_t>(i + xt::Wave::g_firstRamWaveIndex)));
				return;
			}
		}
	}

	void WaveEditorData::onReceiveWave(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg)
	{
		const auto index = toIndex(_data);

		if(!xt::Wave::isValidWaveIndex(index))
			return;

		const auto id = xt::WaveId(index);

		xt::WaveData data;
		xt::State::parseWaveData(data, _msg);

		setWave(id, data);

		if(m_currentWaveRequestIndex == id)
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

		const auto id = xt::TableId(index);

		xt::TableData table;

		xt::State::parseTableData(table, _msg);

		setTable(id, table);

		if(m_currentTableRequestIndex == id)
		{
			m_currentTableRequestIndex = g_invalidTableIndex;
			requestData();
		}
	}

	std::optional<xt::WaveData> WaveEditorData::getWave(xt::WaveId _waveId) const
	{
		auto i = _waveId.rawId();

		if(i < m_romWaves.size())
			return m_romWaves[i];

		if(i < xt::Wave::g_firstRamWaveIndex)
			return {};
		i -= xt::Wave::g_firstRamWaveIndex;
		if(i >= m_ramWaves.size())
			return {};

		if(!m_ramWaves[i])
			return {};

		return m_ramWaves[i];
	}

	xt::WaveId WaveEditorData::getWaveIndex(xt::TableId _tableIndex, xt::TableIndex _indexInTable) const
	{
		if(_tableIndex.rawId() >= m_tables.size())
			return g_invalidWaveIndex;
		if(_indexInTable.rawId() >= std::tuple_size<xt::TableData>())
			return g_invalidWaveIndex;
		const auto table = m_tables[_tableIndex.rawId()];
		if(!table)
			return g_invalidWaveIndex;
		return (*table)[_indexInTable.rawId()];
	}

	std::optional<xt::TableData> WaveEditorData::getTable(xt::TableId _tableIndex) const
	{
		if(_tableIndex.rawId() >= m_tables.size())
			return {};
		return m_tables[_tableIndex.rawId()];
	}

	bool WaveEditorData::swapTableEntries(xt::TableId _table, xt::TableIndex _indexA, xt::TableIndex _indexB)
	{
		if(_indexA == _indexB)
			return false;
		if(_table.rawId() >= m_tables.size())
			return false;
		const auto& table = m_tables[_table.rawId()];
		if(!table)
			return false;
		auto t = *table;
		std::swap(t[_indexA.rawId()], t[_indexB.rawId()]);
		m_tables[_table.rawId()] = t;
		onTableChanged(_table);
		return true;
	}

	bool WaveEditorData::setTableWave(xt::TableId _table, xt::TableIndex _index, xt::WaveId _waveIndex)
	{
		if(_table.rawId() >= m_tables.size())
			return false;
		const auto& table = m_tables[_table.rawId()];
		if(!table)
			return false;
		auto t = *table;
		if(_index.rawId() >= t.size())
			return false;
		if(t[_index.rawId()] == _waveIndex)
			return false;
		t[_index.rawId()] = _waveIndex;
		m_tables[_table.rawId()] = t;
		onTableChanged(_table);
		return true;
	}

	std::optional<xt::WaveData> WaveEditorData::getWave(const xt::TableId _tableIndex, const xt::TableIndex _indexInTable) const
	{
		return getWave(getWaveIndex(_tableIndex, _indexInTable));
	}

	uint16_t WaveEditorData::toIndex(const pluginLib::MidiPacket::Data& _data)
	{
		const uint32_t hh = _data.at(pluginLib::MidiDataType::Bank);
		const uint32_t ll = _data.at(pluginLib::MidiDataType::Program);

		return static_cast<uint16_t>((hh << 7) | ll);
	}

	bool WaveEditorData::requestWave(const xt::WaveId _index)
	{
		if(isWaitingForData())
			return false;

		if(!m_controller.requestWave(_index.rawId()))
			return false;
		m_currentWaveRequestIndex = _index;
		return true;
	}

	bool WaveEditorData::requestTable(const xt::TableId _index)
	{
		if(isWaitingForData())
			return false;

		if(!m_controller.requestTable(_index.rawId()))
			return false;
		m_currentTableRequestIndex = _index;
		return true;
	}

	bool WaveEditorData::setWave(xt::WaveId _id, const xt::WaveData& _data)
	{
		auto i = _id.rawId();

		if(i < m_romWaves.size())
		{
			m_romWaves[i] = _data;
			onWaveChanged(_id);
			return true;
		}
		if(i < xt::Wave::g_firstRamWaveIndex)
			return false;

		i -= xt::Wave::g_firstRamWaveIndex;

		if(i >= m_ramWaves.size())
			return false;

		m_ramWaves[i] = _data;
		onWaveChanged(_id);
		return true;
	}

	bool WaveEditorData::setTable(xt::TableId _index, const xt::TableData& _data)
	{
		if(_index.rawId() >= m_tables.size())
			return false;

		m_tables[_index.rawId()] = _data;
		onTableChanged(_index);
		return true;
	}

	bool WaveEditorData::isAlgorithmicTable(const xt::TableId _index)
	{
		for (const uint32_t i : xt::Wave::g_algorithmicWavetables)
		{
			if(_index.rawId() == i)
				return true;
		}
		return false;
	}

	bool WaveEditorData::isReadOnly(const xt::TableId _table)
	{
		return _table.rawId() < xt::Wave::g_firstRamTableIndex;
	}
}
