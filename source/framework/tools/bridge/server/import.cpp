#include "import.h"

#include "config.h"

#include "baseLib/filesystem.h"

#include "networkLib/logging.h"

#include "synthLib/deviceException.h"
#include "synthLib/os.h"

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#define RTLD_LAZY 0
void* dlopen (const char* _filename, int)
{
	// The path is UTF-8. LoadLibraryA would read it in the ANSI code page and miss a folder with an accented name.
	return LoadLibraryW(baseLib::filesystem::utf8ToWide(_filename).c_str());
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
	namespace
	{
		std::string describe(const bridgeLib::PluginDesc& _desc)
		{
			const auto v = _desc.pluginVersion;
			return _desc.pluginName + ' ' + std::to_string(v / 10000) + '.' + std::to_string(v / 100 % 100) + '.' +
				std::to_string(v % 100) + " (" + _desc.plugin4CC + ')';
		}
	}

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

	synthLib::Device* Import::createDevice(const synthLib::DeviceCreateParams& _params, const bridgeLib::PluginDesc& _desc, std::string& _error)
	{
		std::scoped_lock lock(m_mutex);

		auto it = m_loadedPlugins.find(_desc);

		if(it == m_loadedPlugins.end())
			findPlugins();	// try to load additional plugins if not found

		it = m_loadedPlugins.find(_desc);
		if(it == m_loadedPlugins.end())
		{
			_error = "The server has no " + describe(_desc) + ". Copy that exact version of the plugin, or its server plugin, into the server's plugins folder:\n" + m_config.pluginsPath;
			LOGNET(networkLib::LogLevel::Warning, _error);
			return nullptr;	// still not found
		}

		try
		{
			return it->second.funcCreate(_params);
		}
		catch(synthLib::DeviceException& e)
		{
			_error = "Creating the device of " + describe(_desc) + " failed: " + e.what();
			LOGNET(networkLib::LogLevel::Error, _error << ", code " << static_cast<uint32_t>(e.errorCode()));
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
		// A server plugin is a single library, as are VST2 and, outside macOS, CLAP plugins. VST3 and LV2 plugins are
		// bundle folders that keep their library further down, <name>.vst3/Contents/x86_64-win/<name>.vst3 for example.
		for (const auto* extension : {".dll", ".so", ".dylib", ".vst3", ".clap"})
		{
			std::vector<baseLib::filesystem::FoundFile> files;
			baseLib::filesystem::findFilesRecursive(files, _rootPath, extension, 0, 0, 3);

			for (const auto& file : files)
				loadPlugin(file.path);
		}

#ifdef __APPLE__
		// On macOS every plugin format is a bundle, and its binary has no extension: <name>.vst3/Contents/MacOS/<name>
		std::vector<std::string> entries;
		baseLib::filesystem::getDirectoryEntries(entries, _rootPath);

		for (const auto& entry : entries)
		{
			if(!baseLib::filesystem::isDirectory(entry))
				continue;

			for (const auto* extension : {".vst3", ".clap", ".vst", ".component"})
			{
				if(!baseLib::filesystem::hasExtension(entry, extension))
					continue;

				const auto binary = entry + "/Contents/MacOS/" + baseLib::filesystem::stripExtension(baseLib::filesystem::getFilenameWithoutPath(entry));

				if(baseLib::filesystem::exists(binary))
					loadPlugin(binary);
			}
		}
#endif
	}

	void Import::loadPlugin(const std::string& _file)
	{
		// Look at each file once. A rescan runs whenever a client asks for a plugin we do not have, and libraries that
		// turn out not to be bridge plugins would otherwise be loaded and unloaded again every time. A file replaced
		// while the server runs is therefore only picked up after a restart.
		if(!m_probedFiles.insert(_file).second)
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

		LOGNET(networkLib::LogLevel::Info, "Found plugin " << describe(desc) << " in " << _file);

		m_loadedPlugins.insert({desc, plugin});
	}
}
