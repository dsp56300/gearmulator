#include "weData.h"

#include "xtController.h"

#include "synthLib/midiToSysex.h"
#include "synthLib/os.h"

#include "xtLib/xtState.h"

namespace xtJucePlugin
{
	WaveEditorData::WaveEditorData(Controller& _controller, const std::string& _cacheDir) : m_controller(_controller), m_cacheDir(synthLib::validatePath(_cacheDir))
	{
		loadRomCache();
	}

	void WaveEditorData::requestData()
	{
		if(isWaitingForData())
			return;

		for(uint16_t i=0; i<static_cast<uint16_t>(m_tables.size()); ++i)
		{
			const auto id = xt::TableId(i);
			if(!m_tables[i] && !xt::wave::isAlgorithmicTable(id))
			{
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
				requestWave(xt::WaveId(static_cast<uint16_t>(i + xt::wave::g_firstRamWaveIndex)));
				return;
			}
		}

		onAllDataReceived();
	}

	void WaveEditorData::onReceiveWave(const std::vector<uint8_t>& _msg, const bool _sendToDevice)
	{
		if(!parseMidi(_msg))
			return;

		const auto command = toCommand(_msg);
		const auto id = xt::WaveId(toIndex(_msg));
		
		if(command == xt::SysexCommand::WaveDump &&  m_currentWaveRequestIndex == id)
		{
			m_currentWaveRequestIndex = g_invalidWaveIndex;
			requestData();
		}

		if(_sendToDevice)
			sendWaveToDevice(id);
	}

	void WaveEditorData::onReceiveTable(const std::vector<uint8_t>& _msg, const bool _sendToDevice)
	{
		if(!parseMidi(_msg))
			return;

		const auto command = toCommand(_msg);
		const auto id = xt::TableId(toIndex(_msg));

		if(command == xt::SysexCommand::WaveCtlDump && m_currentTableRequestIndex == id)
		{
			m_currentTableRequestIndex = g_invalidTableIndex;
			requestData();
		}

		if(_sendToDevice)
			sendTableToDevice(id);
	}

	std::optional<xt::WaveData> WaveEditorData::getWave(const xt::WaveId _waveId) const
	{
		auto i = _waveId.rawId();

		if(i < m_romWaves.size())
			return m_romWaves[i];

		if(i < xt::wave::g_firstRamWaveIndex)
			return {};
		i -= xt::wave::g_firstRamWaveIndex;
		if(i >= m_ramWaves.size())
			return {};

		return m_ramWaves[i];
	}

	xt::WaveId WaveEditorData::getWaveIndex(const xt::TableId _tableId, const xt::TableIndex _tableIndex) const
	{
		if(_tableId.rawId() >= m_tables.size())
			return g_invalidWaveIndex;
		if(_tableIndex.rawId() >= std::tuple_size<xt::TableData>())
			return g_invalidWaveIndex;
		const auto table = m_tables[_tableId.rawId()];
		if(!table)
			return g_invalidWaveIndex;
		return (*table)[_tableIndex.rawId()];
	}

	std::optional<xt::TableData> WaveEditorData::getTable(const xt::TableId _tableId) const
	{
		if(_tableId.rawId() >= m_tables.size())
			return {};
		return m_tables[_tableId.rawId()];
	}

	bool WaveEditorData::swapTableEntries(const xt::TableId _tableId, const xt::TableIndex _indexA, const xt::TableIndex _indexB)
	{
		if(_indexA == _indexB)
			return false;
		if(_tableId.rawId() >= m_tables.size())
			return false;
		const auto& table = m_tables[_tableId.rawId()];
		if(!table)
			return false;
		auto t = *table;
		std::swap(t[_indexA.rawId()], t[_indexB.rawId()]);
		m_tables[_tableId.rawId()] = t;
		onTableChanged(_tableId);
		return true;
	}

	bool WaveEditorData::setTableWave(const xt::TableId _tableId, const xt::TableIndex _tableIndex, const xt::WaveId _waveId)
	{
		if(_tableId.rawId() >= m_tables.size())
			return false;
		const auto& table = m_tables[_tableId.rawId()];
		if(!table)
			return false;
		auto t = *table;
		if(_tableIndex.rawId() >= t.size())
			return false;
		if(t[_tableIndex.rawId()] == _waveId)
			return false;
		t[_tableIndex.rawId()] = _waveId;
		m_tables[_tableId.rawId()] = t;
		onTableChanged(_tableId);
		return true;
	}

	bool WaveEditorData::copyTable(const xt::TableId _dest, const xt::TableId _source)
	{
		const auto dst = _dest.rawId();
		const auto src = _source.rawId();

		if(dst >= m_tables.size() || src >= m_tables.size())
			return false;

		auto& srcTable = m_tables[src];
		if(!srcTable)
			return false;
		m_tables[dst] = *srcTable;
		onTableChanged(_dest);
		return true;
	}

	bool WaveEditorData::copyWave(const xt::WaveId _dest, const xt::WaveId _source)
	{
		const auto sourceWave = getWave(_source);
		if(!sourceWave)
			return false;
		return setWave(_dest, *sourceWave);
	}

	std::optional<xt::WaveData> WaveEditorData::getWave(const xt::TableId _tableIndex, const xt::TableIndex _indexInTable) const
	{
		return getWave(getWaveIndex(_tableIndex, _indexInTable));
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
		if(i < xt::wave::g_firstRamWaveIndex)
			return false;

		i -= xt::wave::g_firstRamWaveIndex;

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

	bool WaveEditorData::sendTableToDevice(const xt::TableId _id) const
	{
		const auto index = _id.rawId();
		if(index >= m_tables.size())
			return false;
		auto& table = m_tables[index];
		if(!table)
			return false;
		auto& t = *table;
		const auto sysex = xt::State::createTableData(t, index, false);
		m_controller.sendSysEx(sysex);
		return true;
	}

	bool WaveEditorData::sendWaveToDevice(const xt::WaveId _id) const
	{
		const auto wave = getWave(_id);
		if(!wave)
			return false;
		const auto sysex = xt::State::createWaveData(*wave, _id.rawId(), false);
		m_controller.sendSysEx(sysex);

		// if a wave is edited that is part of the current wavetable of a patch, that table has to be sent again too because the device doesn't update otherwise
		std::set<xt::TableId> dirtyTableIds;

		auto checkTableForPart = [this, _id, &dirtyTableIds](uint8_t p)
		{
			const auto* param = m_controller.getParameter("Wave", p);
			const auto tableId = xt::TableId(static_cast<uint16_t>(param->getUnnormalizedValue()));
			const auto& table = getTable(tableId);
			if(!table)
				return;
			const auto& t = *table;
			for (auto waveId : t)
			{
				if(waveId == _id)
				{
					dirtyTableIds.insert(tableId);
					break;
				}
			}
		};

		if(m_controller.isMultiMode())
		{
			for(uint8_t p=0; p<m_controller.getPartCount(); ++p)
				checkTableForPart(p);
		}
		else
		{
			checkTableForPart(m_controller.getCurrentPart());
		}

		juce::Timer::callAfterDelay(1000, [this, dirtyTableIds]
		{
			for (const auto& id : dirtyTableIds)
			{
				// this is super annyoing. Some kind of dirty mechanism seems to prevent that the wavetable is rebuilt even if we send it again
				// So far the only way to prevent that is to send the table twice, once with another starting wave and then again with the correct
				// one
				const auto waveId = getWaveIndex(id, xt::TableIndex(0));
				const_cast<WaveEditorData*>(this)->setTableWave(id, xt::TableIndex(0), xt::WaveId(waveId.rawId() > 1000 ? 1000 : 1001));
				sendTableToDevice(id);

				juce::Timer::callAfterDelay(1000, [this, id, waveId]
				{
					const_cast<WaveEditorData*>(this)->setTableWave(id, xt::TableIndex(0), waveId);
					sendTableToDevice(id);
				});
			}
		});

		return true;
	}

	void WaveEditorData::getWaveDataForSingle(std::vector<xt::SysEx>& _results, const xt::SysEx& _single) const
	{
		const auto tableId = xt::State::getWavetableFromSingleDump(_single);

		if(xt::wave::isReadOnly(tableId))
			return;

		const auto table = getTable(tableId);

		if(!table)
			return;

		auto& t = *table;

		for (const auto waveId : t)
		{
			if(!xt::wave::isValidWaveIndex(waveId.rawId()))
				continue;

			if(xt::wave::isReadOnly(waveId))
				continue;

			const auto wave = getWave(waveId);
			if(!wave)
				continue;

			const auto& w = *wave;

			_results.emplace_back(xt::State::createWaveData(w, waveId.rawId(), false));
		}

		_results.emplace_back(xt::State::createTableData(t, tableId.rawId(), false));
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

	void WaveEditorData::onAllDataReceived() const
	{
		saveRomCache();
	}

	xt::SysexCommand WaveEditorData::toCommand(const std::vector<uint8_t>& _sysex)
	{
		return static_cast<xt::SysexCommand>(_sysex[4]);
	}

	uint16_t WaveEditorData::toIndex(const std::vector<uint8_t>& _sysex)
	{
		const uint32_t hh = _sysex[5];
		const uint32_t ll = _sysex[6];

		const uint16_t index = static_cast<uint16_t>((hh << 7) | ll);

		return index;
	}

	bool WaveEditorData::parseMidi(const std::vector<uint8_t>& _sysex)
	{
		if(_sysex.size() < 10 || _sysex.front() != 0xf0 || _sysex.back() != 0xf7)
			return false;
		if(_sysex[1] != wLib::IdWaldorf)
			return false;
		if(_sysex[2] != xt::IdMw2)
			return false;

		const auto cmd = toCommand(_sysex);
		const auto index = toIndex(_sysex);

		switch (cmd)  // NOLINT(clang-diagnostic-switch-enum)
		{
		case xt::SysexCommand::WaveDump:
			{
				if(!xt::wave::isValidWaveIndex(index))
					return false;

				const auto id = xt::WaveId(index);

				xt::WaveData data;
				xt::State::parseWaveData(data, _sysex);

				setWave(id, data);
			}
			return true;
		case xt::SysexCommand::WaveCtlDump:
			{
				if(!xt::wave::isValidTableIndex(index))
					return false;

				const auto id = xt::TableId(index);

				xt::TableData table;

				xt::State::parseTableData(table, _sysex);

				setTable(id, table);
			}
			return true;
		default:
			return false;
		}
	}

	std::string WaveEditorData::getRomCacheFilename() const
	{
		return m_cacheDir + "romWaves.syx";
	}

	void WaveEditorData::saveRomCache() const
	{
		const auto romWaves = getRomCacheFilename();

		std::vector<uint8_t> data;

		for(uint16_t i=0; i<static_cast<uint16_t>(m_romWaves.size()); ++i)
		{
			auto& romWave = m_romWaves[i];
			assert(romWave);
			if(!romWave)
				continue;
			auto sysex = xt::State::createWaveData(*romWave, i, false);
			data.insert(data.end(), sysex.begin(), sysex.end());
		}

		assert(xt::wave::g_firstRamTableIndex < m_tables.size());

		for(uint16_t i=0; i<xt::wave::g_firstRamTableIndex; ++i)
		{
			auto& table = m_tables[i];
			assert(table || xt::wave::isAlgorithmicTable(xt::TableId(i)));
			if(!table)
				continue;
			auto sysex = xt::State::createTableData(*table, i, false);
			data.insert(data.end(), sysex.begin(), sysex.end());
		}
		synthLib::writeFile(romWaves, data);
	}

	void WaveEditorData::loadRomCache()
	{
		std::vector<uint8_t> data;
		if(!synthLib::readFile(data, getRomCacheFilename()))
			return;

		std::vector<std::vector<uint8_t>> sysexMessages;
		synthLib::MidiToSysex::splitMultipleSysex(sysexMessages, data);
		for (const auto& sysex : sysexMessages)
			parseMidi(sysex);
	}
}
