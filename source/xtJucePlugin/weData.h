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
		pluginLib::Event<uint32_t> onWaveChanged;
		pluginLib::Event<uint32_t> onTableChanged;

		WaveEditorData(Controller& _controller);

		void requestData();

		bool isWaitingForWave() const { return m_currentWaveRequestIndex != g_invalidWaveIndex; }
		bool isWaitingForTable() const { return m_currentTableRequestIndex != g_invalidWaveIndex; }
		bool isWaitingForData() const { return isWaitingForWave() || isWaitingForTable(); }

		void onReceiveWave(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg);
		void onReceiveTable(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg);

		std::optional<xt::WaveData> getWave(uint32_t _waveIndex) const;
		std::optional<xt::WaveData> getWave(uint32_t _tableIndex, uint32_t _indexInTable) const;

		uint32_t getWaveIndex(uint32_t _tableIndex, uint32_t _indexInTable) const;

		std::optional<xt::TableData> getTable(uint32_t _tableIndex) const;
		bool swapTableEntries(uint32_t _table, uint32_t _indexA, uint32_t _indexB);
		bool setTableWave(uint32_t _table, uint32_t _index, uint32_t _waveIndex);

		static uint32_t toIndex(const pluginLib::MidiPacket::Data& _data);
		static bool isAlgorithmicTable(uint32_t _index);

	private:

		bool requestWave(uint32_t _index);
		bool requestTable(uint32_t _index);

		bool setWave(uint32_t _index, const xt::WaveData& _data);
		bool setTable(uint32_t _index, const xt::TableData& _data);

		Controller& m_controller;

		uint32_t m_currentWaveRequestIndex = g_invalidWaveIndex;
		uint32_t m_currentTableRequestIndex = g_invalidWaveIndex;

		std::array<std::optional<xt::WaveData>, xt::Wave::g_romWaveCount> m_romWaves;
		std::array<std::optional<xt::WaveData>, xt::Wave::g_ramWaveCount> m_ramWaves;
		std::array<std::optional<xt::TableData>, xt::Wave::g_tableCount> m_tables;
	};
}
