#include "import.h"

#include "config.h"
#include "networkLib/logging.h"
#include "synthLib/deviceException.h"
#include "synthLib/os.h"

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#define RTLD_LAZY 0
void* dlopen (const char* _filename, int)
{
	return LoadLibraryA(_filename);
}
FARPROC dlsym (void* _handle, const char* _name)
{
	return GetProcAddress (static_cast<HMODULE>(_handle), _name);
}
int dlclose(void* _handle)
{
	return FreeLibrary(static_cast<HMODULE>(_handle));
}
#else
#include <dlfcn.h>
#endif

namespace bridgeServer
{
	Import::Import(const Config& _config) : m_config(_config)
	{
		findPlugins();
	}

	Import::~Import()
	{
		for (const auto& it : m_loadedPlugins)
			dlclose(it.second.handle);
		m_loadedPlugins.clear();
	}

	synthLib::Device* Import::createDevice(const synthLib::DeviceCreateParams& _params, const bridgeLib::PluginDesc& _desc)
	{
		std::scoped_lock lock(m_mutex);

		auto it = m_loadedPlugins.find(_desc);

		if(it == m_loadedPlugins.end())
			findPlugins();	// try to load additional plugins if not found

		it = m_loadedPlugins.find(_desc);
		if(it == m_loadedPlugins.end())
		{
			LOGNET(networkLib::LogLevel::Warning, "Failed to create device for plugin '" << _desc.pluginName << "', version " << _desc.pluginVersion << ", id " << _desc.plugin4CC << ", no matching plugin available");
			return nullptr;	// still not found
		}

		try
		{
			return it->second.funcCreate(_params);
		}
		catch(synthLib::DeviceException& e)
		{
			LOGNET(networkLib::LogLevel::Error, "Failed to create device for plugin '" << _desc.pluginName << "', version " << _desc.pluginVersion << ", id " << _desc.plugin4CC << 
				", device creation caused exception: code " << static_cast<uint32_t>(e.errorCode()) << ", message: " << e.what());
			return nullptr;
		}
	}

	bool Import::destroyDevice(const bridgeLib::PluginDesc& _desc, synthLib::Device* _device)
	{
		if(!_device)
			return true;

		std::scoped_lock lock(m_mutex);

		const auto it = m_loadedPlugins.find(_desc);
		if(it == m_loadedPlugins.end())
		{
			assert(false && "plugin unloaded before device destroyed");
			return false;
		}
		it->second.funcDestroy(_device);
		return true;
	}

	void Import::findPlugins()
	{
		findPlugins(m_config.pluginsPath);
		findPlugins(synthLib::getModulePath() + "plugins/");
	}

	void Import::findPlugins(const std::string& _rootPath)
	{
		findPlugins(_rootPath, ".dll");
		findPlugins(_rootPath, ".so");
		findPlugins(_rootPath, ".dylib");

		findPlugins(_rootPath, ".vst3");
		findPlugins(_rootPath, ".clap");
		findPlugins(_rootPath, ".lv2");
	}

	void Import::findPlugins(const std::string& _rootPath, const std::string& _extension)
	{
		const auto path = synthLib::getModulePath() + "plugins/";
		std::vector<std::string> files;
		synthLib::findFiles(files, path, _extension, 0, std::numeric_limits<uint32_t>::max());

		for (const auto& file : files)
			loadPlugin(file);
	}

	void Import::loadPlugin(const std::string& _file)
	{
		// load each plugin lib only once
		if(m_loadedFiles.find(_file) != m_loadedFiles.end())
			return;

		Plugin plugin;

		plugin.handle = dlopen(_file.c_str(), RTLD_LAZY);
		if(!plugin.handle)
			return;

		plugin.funcCreate = reinterpret_cast<FuncBridgeDeviceCreate>(dlsym(plugin.handle, "bridgeDeviceCreate")); // NOLINT(clang-diagnostic-cast-function-type-strict)
		plugin.funcDestroy = reinterpret_cast<FuncBridgeDeviceDestroy>(dlsym(plugin.handle, "bridgeDeviceDestroy")); // NOLINT(clang-diagnostic-cast-function-type-strict)
		plugin.funcGetDesc = reinterpret_cast<FuncBridgeDeviceGetDesc>(dlsym(plugin.handle, "bridgeDeviceGetDesc")); // NOLINT(clang-diagnostic-cast-function-type-strict)

		if(!plugin.funcCreate || !plugin.funcDestroy || !plugin.funcGetDesc)
		{
			dlclose(plugin.handle);
			return;
		}

		bridgeLib::PluginDesc desc;
		plugin.funcGetDesc(desc);

		if(desc.plugin4CC.empty() || desc.pluginName.empty() || desc.pluginVersion == 0)
		{
			dlclose(plugin.handle);
			return;
		}

		if(m_loadedPlugins.find(desc) != m_loadedPlugins.end())
		{
			dlclose(plugin.handle);
			return;
		}

		LOGNET(networkLib::LogLevel::Info, "Found plugin '" << desc.pluginName << "', version " << desc.pluginVersion << ", id " << desc.plugin4CC);

		m_loadedPlugins.insert({desc, plugin});
		m_loadedFiles.insert(_file);
	}
}
