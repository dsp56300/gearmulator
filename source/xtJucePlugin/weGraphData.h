#pragma once

#include "weTypes.h"
#include <tuple>

#include "../jucePluginLib/event.h"

namespace xtJucePlugin
{
	class GraphData
	{
	public:
		pluginLib::Event<WaveData> onSourceChanged;

		GraphData() : m_source({}), m_data({}), m_frequencies({}), m_phases({})
		{
		}

		void set(const WaveData& _data);

		const auto& getData() const { return m_data; }
		const auto& getFrequencies() const { return m_frequencies; }
		const auto& getPhases() const { return m_phases; }

	private:
		WaveData m_source;

		std::array<float, std::tuple_size_v<WaveData>> m_data;
		std::array<float, std::tuple_size_v<WaveData>/2> m_frequencies;
		std::array<float, std::tuple_size_v<WaveData>/2> m_phases;
	};
}
