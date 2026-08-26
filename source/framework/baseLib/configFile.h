#pragma once

#include "propertyMap.h"

namespace baseLib
{
	class ConfigFile : public PropertyMap
	{
	public:
		ConfigFile() = default;
		explicit ConfigFile(const char* _filename);
		explicit ConfigFile(const std::string& _filename);

		bool writeToFile(const std::string& _filename) const;
	};
}
