#pragma once

#include <cstdint>
#include <optional>
#include <limits>
#include <vector>

#include "weTypes.h"

#include "xtLib/xtMidiTypes.h"

#include "jucePluginLib/midipacket.h"
#include "jucePluginLib/event.h"

class Controller;

namespace xtJucePlugin
{
	class WaveEditorData
	{
	public:
		static constexpr uint32_t InvalidWaveIndex = std::numeric_limits<uint32_t>::max();

		pluginLib::Event<uint32_t> onWaveChanged;
		pluginLib::Event<uint32_t> onTableChanged;

		WaveEditorData(Controller& _controller);

		void requestData();

		bool isWaitingForWave() const { return m_currentWaveRequestIndex != InvalidWaveIndex; }
		bool isWaitingForTable() const { return m_currentTableRequestIndex != InvalidWaveIndex; }
		bool isWaitingForData() const { return isWaitingForWave() || isWaitingForTable(); }

		void onReceiveWave(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg);
		void onReceiveTable(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg);
		std::optional<WaveData> getWave(uint32_t _waveIndex) const;
		uint32_t getWaveIndex(uint32_t _tableIndex, uint32_t _indexInTable) const;
		std::optional<WaveData> getWave(uint32_t _tableIndex, uint32_t _indexInTable) const;

		static uint32_t toIndex(const pluginLib::MidiPacket::Data& _data);
		static bool isAlgorithmicTable(uint32_t _index);

	private:

		bool requestWave(uint32_t _index);
		bool requestTable(uint32_t _index);

		bool setWave(uint32_t _index, const WaveData& _data);
		bool setTable(uint32_t _index, const TableData& _data);

		Controller& m_controller;

		uint32_t m_currentWaveRequestIndex = InvalidWaveIndex;
		uint32_t m_currentTableRequestIndex = InvalidWaveIndex;

		std::array<std::optional<WaveData>, xt::Wave::g_romWaveCount> m_romWaves;
		std::array<std::optional<WaveData>, xt::Wave::g_ramWaveCount> m_ramWaves;
		std::array<std::optional<TableData>, xt::Wave::g_tableCount> m_tables;
	};
}
