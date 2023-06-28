#pragma once

#define MQ_VOICE_EXPANSION 0

namespace mqLib
{
	static constexpr bool g_pluginDemo = false;

#if MQ_VOICE_EXPANSION
	static constexpr bool g_useVoiceExpansion = true;
#else
	static constexpr bool g_useVoiceExpansion = false;
#endif
}


