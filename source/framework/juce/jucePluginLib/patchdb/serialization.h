#pragma once

#include <string>
#include <vector>

namespace pluginLib::patchDB
{
	class Serialization
	{
	public:
		static std::vector<std::string> split(const std::string& _string, char _delim);
	};
}
