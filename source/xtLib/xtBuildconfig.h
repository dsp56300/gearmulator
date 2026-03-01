#pragma once

#include <cstdint>

#define XT_VOICE_EXPANSION 1

namespace xt
{
	static constexpr bool g_pluginDemo = false;

#if XT_VOICE_EXPANSION
	static constexpr bool g_useVoiceExpansion = true;
#else
	static constexpr bool g_useVoiceExpansion = false;
#endif

	static constexpr uint32_t g_dspCount = g_useVoiceExpansion ? 3 : 1;

	// Expansion DSP index mapping (verified from actual XT ROM dumps):
	// idx 0 = HDI08B (0xFC000) = exp1: voices, ESSI0 RX (ext audio) + ESSI1 TX/RX
	// idx 1 = HDI08C (0xFD000) = exp2: voices + mixing, ESSI1 only (no ESSI0)
	// idx 2 = HDI08A (0xFE000) = exp3: effects + final output, ESSI0 TX + ESSI1 TX/RX
	// ESSI1 ring: DSP2 TX → DSP0 RX → DSP0 TX → DSP1 RX → DSP1 TX → DSP2 RX
	static constexpr uint32_t g_mainDspIdx = g_useVoiceExpansion ? 2 : 0;
}
