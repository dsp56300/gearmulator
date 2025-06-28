#pragma once

#include <map>
#include <string>

namespace rmlPlugin::skinConverter
{
	struct SkinConverterOptions
	{
		std::map<std::string, std::string> idReplacements;
	};
}
