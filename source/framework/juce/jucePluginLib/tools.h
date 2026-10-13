#pragma once

#include <string>

namespace pluginLib
{
	class Tools
	{
	public:
		static std::string getPublicDataFolder(const std::string& _vendorName, const std::string& _productName);
	};
}
