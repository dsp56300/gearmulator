#pragma once

#include <map>
#include <mutex>
#include <set>

#include "bridgeLib/commands.h"

namespace synthLib
{
	struct DeviceCreateParams;
	class Device;
}

namespace bridgeServer
{
	struct Config;

	class Import
	{
	public:
		typedef synthLib::Device* (*FuncBridgeDeviceCreate)(const synthLib::DeviceCreateParams& _params);
		typedef void (*FuncBridgeDeviceDestroy)(synthLib::Device*);
		typedef void (*FuncBridgeDeviceGetDesc)(bridgeLib::PluginDesc&);

		struct Plugin final
		{
			std::string filename;
			void* handle = nullptr;
			FuncBridgeDeviceCreate funcCreate = nullptr;
			FuncBridgeDeviceDestroy funcDestroy = nullptr;
			FuncBridgeDeviceGetDesc funcGetDesc = nullptr;
		};

		Import(const Config& _config);
		~Import();

		// Returns nullptr and says why in _error if there is no matching plugin or it fails to create its device
		synthLib::Device* createDevice(const synthLib::DeviceCreateParams& _params, const bridgeLib::PluginDesc& _desc, std::string& _error);
		bool destroyDevice(const bridgeLib::PluginDesc& _desc, synthLib::Device* _device);

	private:
		void findPlugins();
		void findPlugins(const std::string& _rootPath);
		void loadPlugin(const std::string& _file);

		const Config& m_config;

		std::map<bridgeLib::PluginDesc, Plugin> m_loadedPlugins;
		std::set<std::string> m_probedFiles;

		std::mutex m_mutex;
	};
}
