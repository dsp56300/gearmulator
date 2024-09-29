#pragma once

#include <cstdint>
#include <optional>
#include <limits>
#include <vector>

#include "weTypes.h"

#include "xtLib/xtMidiTypes.h"

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

		WaveEditorData(Controller& _controller);

		void requestData();

		bool isWaitingForWave() const { return m_currentWaveRequestIndex != g_invalidWaveIndex; }
		bool isWaitingForTable() const { return m_currentTableRequestIndex != g_invalidTableIndex; }
		bool isWaitingForData() const { return isWaitingForWave() || isWaitingForTable(); }

		void onReceiveWave(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg);
		void onReceiveTable(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg);

		std::optional<xt::WaveData> getWave(xt::WaveId _waveId) const;
		std::optional<xt::WaveData> getWave(xt::TableId _tableIndex, xt::TableIndex _indexInTable) const;

		xt::WaveId getWaveIndex(xt::TableId _tableId, xt::TableIndex _tableIndex) const;

		std::optional<xt::TableData> getTable(xt::TableId _tableId) const;
		bool swapTableEntries(xt::TableId _tableId, xt::TableIndex _indexA, xt::TableIndex _indexB);
		bool setTableWave(xt::TableId _tableId, xt::TableIndex _tableIndex, xt::WaveId _waveId);

		bool copyTable(const xt::TableId _dest, const xt::TableId _source);
		bool copyWave(const xt::WaveId _dest, const xt::WaveId _source);

		static uint16_t toIndex(const pluginLib::MidiPacket::Data& _data);

		static bool isAlgorithmicTable(xt::TableId _index);
		static bool isReadOnly(xt::TableId _table);
		static bool isReadOnly(xt::WaveId _waveId);
		static bool isReadOnly(xt::TableIndex _index);

		bool setWave(xt::WaveId _id, const xt::WaveData& _data);
		bool setTable(xt::TableId _index, const xt::TableData& _data);

	private:
		bool requestWave(xt::WaveId _index);
		bool requestTable(xt::TableId _index);

		Controller& m_controller;

		xt::WaveId m_currentWaveRequestIndex = g_invalidWaveIndex;
		xt::TableId m_currentTableRequestIndex = g_invalidTableIndex;

		std::array<std::optional<xt::WaveData>, xt::Wave::g_romWaveCount> m_romWaves;
		std::array<std::optional<xt::WaveData>, xt::Wave::g_ramWaveCount> m_ramWaves;
		std::array<std::optional<xt::TableData>, xt::Wave::g_tableCount> m_tables;
	};
}
