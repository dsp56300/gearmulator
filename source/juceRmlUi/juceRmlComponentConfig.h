#pragma once

#include <cstdint>

namespace juceRmlUi
{
	enum class SoftwareRendererMode : int8_t
	{
		Auto = -1,
		ForceOff = 0,
		ForceOn = 1,
	};

	struct RmlComponentConfig
	{
		int refreshRateLimitHz = -1;
		SoftwareRendererMode forceSoftwareRenderer = SoftwareRendererMode::Auto;
	};
}
