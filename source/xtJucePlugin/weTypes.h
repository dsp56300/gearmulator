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
}
