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

		synthLib::Device* createDevice(const synthLib::DeviceCreateParams& _params, const bridgeLib::PluginDesc& _desc);
		bool destroyDevice(const bridgeLib::PluginDesc& _desc, synthLib::Device* _device);

	private:
		void findPlugins();
		void findPlugins(const std::string& _rootPath);
		void findPlugins(const std::string& _rootPath, const std::string& _extension);
		void loadPlugin(const std::string& _file);

		const Config& m_config;

		std::map<bridgeLib::PluginDesc, Plugin> m_loadedPlugins;
		std::set<std::string> m_loadedFiles;

		std::mutex m_mutex;
	};
}
