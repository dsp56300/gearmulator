#pragma once

#include "xtLib/xtTypes.h"

namespace xtJucePlugin
{
	enum class WaveCategory
	{
		Invalid = -1,

		Rom,
		User,
		Plugin,

		Count
	};

	static constexpr float g_waveEditorScale = 2.0f * 1.3f;

	static constexpr uint32_t g_invalidIndex = std::numeric_limits<uint32_t>::max();
	static constexpr xt::WaveId g_invalidWaveIndex = xt::WaveId::invalid();
	static constexpr xt::TableId g_invalidTableIndex = xt::TableId::invalid();
}
