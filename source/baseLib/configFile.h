#pragma once

#include "propertyMap.h"

namespace baseLib
{
	class ConfigFile : public PropertyMap
	{
	public:
		explicit ConfigFile(const char* _filename);
		explicit ConfigFile(const std::string& _filename);
	};
}
