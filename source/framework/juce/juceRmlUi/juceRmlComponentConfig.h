#pragma once

#include <cstdint>
#include <vector>

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
		// macOS only: Metal is used whenever it is supported. Set this to fall back to OpenGL instead,
		// which is useful to find out whether a rendering problem is specific to the Metal backend.
		bool disableMetalRenderer = false;
		std::vector<std::string> additionalTemplateFiles;
	};
}
