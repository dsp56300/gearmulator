#pragma once

#include <string>
#include <vector>

namespace pluginManager
{
	enum class PluginFormat
	{
		VST2,
		VST3,
		CLAP,
		LV2,
		AU,
		Standalone,

		Count
	};

	struct PlatformUtils
	{
		static std::string getCurrentOS();
		static std::string getCurrentArch();

		// Empty when the format has no install location on this platform. Standalone apps have
		// no conventional destination, so they are reported as not installable for now.
		// ponytail: pick a standalone install location once we decide one per OS.
		static std::string getDefaultPluginPath(PluginFormat _format);

		// Everywhere a build of this format is normally kept on this system, the default above
		// first. For finding one that is already installed, which is nowhere near guaranteed to be
		// in the one folder this would install it to: macOS keeps most plugins in the system
		// /Library/Audio/Plug-Ins while the default here is the user's own, Linux has a /usr tree
		// beside the ~/.vst3 default, and VST2 never had a standard place anywhere.
		//
		// Only for looking. Where an install goes is still the location the settings name, and that
		// folder is searched before any of these - see PluginManager::locateInstalled.
		static std::vector<std::string> getSearchPaths(PluginFormat _format);

		// Looks somewhere else instead, which is how a test works on this without a plugin folder of
		// the machine it runs on - the same reason setSettingsFolder exists. An empty list puts the
		// real locations back.
		static void setSearchPaths(PluginFormat _format, std::vector<std::string> _paths);

		static std::vector<PluginFormat> getAvailableFormats();

		static std::string formatToString(PluginFormat _format);
		static PluginFormat formatFromString(const std::string& _format);

		// Everything a product keeps of its own: Documents/The Usual Suspects/<product>/, with
		// config/, patchmanager/, skins/ and roms/ beneath it. Worked out the same way the plugin
		// works it out - see pluginLib::Processor::getDataFolder - including the TUS_DATA_FOLDER
		// override. _product is the one whose folder it is, which for an effect build is the plugin it
		// extends: Catalog::dataProductOf, because the name alone does not say which builds those are.
		static std::string dataFolder(const std::string& _product);

		// Where the plugin itself will look for its ROM: Documents/The Usual Suspects/<product>/roms/.
		// _product as for dataFolder.
		static std::string romFolder(const std::string& _product);

		// Everything this program keeps per installation lives under here: the settings in config/,
		// the backups in backups/. Ends with a separator. --settings moves all of it together,
		// which is the point of that switch.
		static std::string settingsFolder();

		// Where this program keeps a settings file of its own.
		static std::string getSettingsPath(const std::string& _fileName);

		// Takes a file away from everybody but the user it belongs to.
		//
		// A file written with the usual umask is 644 on macOS and Linux - readable by every account
		// on the machine. That is how the logins came to be world-readable (BUG-10460). On Windows
		// a file under the user's own profile already inherits an ACL that nobody else can read,
		// so there is nothing to do there.
		static void restrictToOwner(const std::string& _path);

		// Puts the settings somewhere else, which is how a test works on them without touching the
		// settings of the machine it runs on. An empty folder puts them back where they belong.
		static void setSettingsFolder(const std::string& _folder);

		// What a build of this format is called once installed, e.g. Osirus.vst3. Empty when the
		// format has no name here, which is every format that cannot be installed.
		static std::string pluginFileName(const std::string& _pluginId, PluginFormat _format);

		// _dir is the location the format is installed to, which is a setting - see InstallPaths.
		static std::string installedFilePath(const std::string& _pluginId, PluginFormat _format, const std::string& _dir);
		static bool isPluginInstalled(const std::string& _pluginId, PluginFormat _format, const std::string& _dir);

		// What the installed file says its version is, empty when it does not say. Only some formats
		// carry it: on Windows the JUCE-built ones have a version resource but CLAP has none, on macOS
		// every format is a bundle and answers, on Linux nothing does. InstalledPlugins is what knows
		// the rest.
		static std::string getInstalledVersion(const std::string& _pluginId, PluginFormat _format, const std::string& _dir);

		// Who made what is at _path, when that is somebody other than us - empty when it is ours or
		// nothing says. Another maker's plugin can carry the very name one of ours installs under, and
		// an install over one deleted it. Nothing is replaced, removed or taken out of a file this
		// answers for.
		//
		// The bundle identifier on macOS, the company in the version resource on Windows; both are
		// left empty by some of our builds (a CLAP has no version resource), which is why only a
		// name that is there and is not ours counts. An ELF names nobody, so on Linux this is empty.
		static std::string otherMaker(const std::string& _path);
	};
}
