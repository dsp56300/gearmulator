#pragma once

#include <map>
#include <string>

namespace rmlPlugin::skinConverter
{
	struct SkinConverterOptions
	{
		std::map<std::string, std::string> idReplacements;
		std::vector<std::string> includeStyles;
		uint32_t maxTextureWidth = 2048;
		uint32_t maxTextureHeight = 2048;
	};
}
