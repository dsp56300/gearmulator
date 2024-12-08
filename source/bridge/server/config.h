#pragma once

#include <cstdint>
#include <string>

namespace bridgeServer
{
	struct Config
	{
		Config(int _argc, char** _argv);

		uint32_t portTcp;
		uint32_t portUdp;
		uint32_t deviceStateRefreshMinutes;
		std::string pluginsPath;
		std::string romsPath;

		static std::string getDefaultDataPath();
	};
}
