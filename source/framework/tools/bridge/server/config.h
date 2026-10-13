#pragma once

#include <cstdint>
#include <string>

namespace bridgeServer
{
	struct Config
	{
		Config(int _argc, char** _argv);

		uint32_t portTcp;
		uint32_t deviceStateRefreshMinutes;
		std::string pluginsPath;
		std::string romsPath;
		std::string devicesPath;	// a folder per product, the home path of its devices

		static std::string getDefaultDataPath();
	};
}
