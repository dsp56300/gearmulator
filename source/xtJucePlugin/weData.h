#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "weTypes.h"

#include "xtLib/xtMidiTypes.h"
#include "xtLib/xtState.h"

#include "jucePluginLib/midipacket.h"
#include "jucePluginLib/event.h"

namespace xtJucePlugin
{
	class Controller;

	class WaveEditorData
	{
	public:
		pluginLib::Event<xt::WaveId> onWaveChanged;
		pluginLib::Event<xt::TableId> onTableChanged;

		WaveEditorData(Controller& _controller, const std::string& _cacheDir);

		void requestData();

		bool isWaitingForWave() const { return m_currentWaveRequestIndex != g_invalidWaveIndex; }
		bool isWaitingForTable() const { return m_currentTableRequestIndex != g_invalidTableIndex; }
		bool isWaitingForData() const { return isWaitingForWave() || isWaitingForTable(); }

		void onReceiveWave(const std::vector<uint8_t>& _msg, bool _sendToDevice = false);
		void onReceiveTable(const std::vector<uint8_t>& _msg, bool _sendToDevice = false);

		std::optional<xt::WaveData> getWave(xt::WaveId _waveId) const;
		std::optional<xt::WaveData> getWave(xt::TableId _tableIndex, xt::TableIndex _indexInTable) const;

		xt::WaveId getWaveIndex(xt::TableId _tableId, xt::TableIndex _tableIndex) const;

		std::optional<xt::TableData> getTable(xt::TableId _tableId) const;
		bool swapTableEntries(xt::TableId _tableId, xt::TableIndex _indexA, xt::TableIndex _indexB);
		bool setTableWave(xt::TableId _tableId, xt::TableIndex _tableIndex, xt::WaveId _waveId);

		bool copyTable(xt::TableId _dest, xt::TableId _source);
		bool copyWave(xt::WaveId _dest, xt::WaveId _source);

		bool setWave(xt::WaveId _id, const xt::WaveData& _data);
		bool setTable(xt::TableId _index, const xt::TableData& _data);

		bool sendTableToDevice(xt::TableId _id) const;
		bool sendWaveToDevice(xt::WaveId _id) const;

		void getWaveDataForSingle(std::vector<xt::SysEx>& _results, const xt::SysEx& _single) const;

	private:
		bool requestWave(xt::WaveId _index);
		bool requestTable(xt::TableId _index);

		void onAllDataReceived() const;

		static xt::SysexCommand toCommand(const std::vector<uint8_t>& _sysex);
		static uint16_t toIndex(const std::vector<uint8_t>& _sysex);
		bool parseMidi(const std::vector<uint8_t>& _sysex);

		std::string getRomCacheFilename() const;
		void saveRomCache() const;
		void loadRomCache();

		Controller& m_controller;
		const std::string m_cacheDir;

		xt::WaveId m_currentWaveRequestIndex = g_invalidWaveIndex;
		xt::TableId m_currentTableRequestIndex = g_invalidTableIndex;

		std::array<std::optional<xt::WaveData>, xt::wave::g_romWaveCount> m_romWaves;
		std::array<std::optional<xt::WaveData>, xt::wave::g_ramWaveCount> m_ramWaves;
		std::array<std::optional<xt::TableData>, xt::wave::g_tableCount> m_tables;
	};
}
