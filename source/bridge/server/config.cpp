#include "config.h"

#include "server.h"
#include "baseLib/commandline.h"
#include "baseLib/configFile.h"

#include "synthLib/os.h"

namespace bridgeServer
{
	Config::Config(int _argc, char** _argv)
		: portTcp(bridgeLib::g_tcpServerPort)
		, portUdp(bridgeLib::g_udpServerPort)
		, deviceStateRefreshMinutes(3)
		, pluginsPath(getDefaultDataPath() + "plugins/")
		, romsPath(getDefaultDataPath() + "roms/")
	{
		const baseLib::CommandLine commandLine(_argc, _argv);

		auto configFilename = commandLine.get("config");

		if (configFilename.empty())
			configFilename = getDefaultDataPath() + "config/dspBridgeServer.cfg";

		baseLib::ConfigFile config(configFilename);

		config.add(commandLine, true);

		portTcp = config.getInt("tcpPort", static_cast<int>(portTcp));
		portUdp = config.getInt("tcpPort", static_cast<int>(portUdp));
		deviceStateRefreshMinutes = config.getInt("deviceStateRefreshMinutes", static_cast<int>(deviceStateRefreshMinutes));
		pluginsPath = config.get("pluginsPath", pluginsPath);
		romsPath = config.get("romsPath", romsPath);

		synthLib::createDirectory(pluginsPath);
		synthLib::createDirectory(romsPath);
	}

	std::string Config::getDefaultDataPath()
	{
		return synthLib::validatePath(synthLib::getSpecialFolderPath(synthLib::SpecialFolderType::UserDocuments)) + "The Usual Suspects/dspBridgeServer/";
	}
}
