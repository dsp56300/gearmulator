#pragma once

#include <string>

namespace pluginLib
{
	class Tools
	{
	public:
		static bool isHeadless();

		static std::string getPublicDataFolder(const std::string& _productName);
	};
}
