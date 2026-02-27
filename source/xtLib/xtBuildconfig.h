#pragma once

#define XT_VOICE_EXPANSION 0

namespace xt
{
	static constexpr bool g_pluginDemo = false;

#if XT_VOICE_EXPANSION
	static constexpr bool g_useVoiceExpansion = true;
#else
	static constexpr bool g_useVoiceExpansion = false;
#endif
}
