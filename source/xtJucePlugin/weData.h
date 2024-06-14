#pragma once

#include <cstdint>
#include <optional>
#include <limits>
#include <vector>

#include "weTypes.h"

#include "../xtLib/xtMidiTypes.h"

#include "../jucePluginLib/midipacket.h"
#include "../jucePluginLib/event.h"

class Controller;

namespace xtJucePlugin
{
	class WaveEditorData
	{
	public:
		static constexpr uint32_t InvalidWaveIndex = std::numeric_limits<uint32_t>::max();

		pluginLib::Event<uint32_t> onWaveChanged;

		WaveEditorData(Controller& _controller);

		void requestData();

		bool isWaitingForWave() const { return m_currentWaveRequestIndex != InvalidWaveIndex; }
		void onReceiveWave(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg);

	private:
		bool requestWave(uint32_t _index);
		bool setWave(uint32_t _index, const WaveData& _data);

		Controller& m_controller;

		uint32_t m_currentWaveRequestIndex = InvalidWaveIndex;

		std::array<std::optional<WaveData>, xt::Wave::g_romWaveCount> m_romWaves;
		std::array<std::optional<WaveData>, xt::Wave::g_ramWaveCount> m_ramWaves;
	};
}
