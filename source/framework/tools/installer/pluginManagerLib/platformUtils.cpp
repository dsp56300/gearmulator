#include "platformUtils.h"

#include "baseLib/filesystem.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <string_view>

#if !JUCE_WINDOWS
#include <sys/stat.h>
#endif

#include "juce_core/juce_core.h"

#if JUCE_WINDOWS
#include <windows.h>
#pragma comment(lib, "version.lib")
#elif JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace pluginManager
{
	std::string PlatformUtils::getCurrentOS()
	{
#if JUCE_WINDOWS
		return "windows";
#elif JUCE_MAC
		return "macos";
#elif JUCE_LINUX
		return "linux";
#else
		return "unknown";
#endif
	}

	std::string PlatformUtils::getCurrentArch()
	{
#if JUCE_MAC
		return "universal";
#elif defined(__aarch64__) || defined(_M_ARM64)
		return "aarch64";
#else
		return "x86_64";
#endif
	}

	std::string PlatformUtils::getDefaultPluginPath(PluginFormat _format)
	{
#if JUCE_WINDOWS
		const auto programFiles = juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory).getFullPathName().toStdString();
		const auto commonFiles = programFiles + "\\Common Files";

		switch (_format)
		{
		case PluginFormat::VST2:  return programFiles + "\\VSTPlugins";
		case PluginFormat::VST3:  return commonFiles + "\\VST3";
		case PluginFormat::CLAP:  return commonFiles + "\\CLAP";
		case PluginFormat::LV2:   return commonFiles + "\\LV2";

		// Not under Common Files: that is where hosts go looking, and this is a program rather
		// than something to be found. Its own folder beside them, which is where somebody
		// hunting for it would look, and which the install already knows how to ask for.
		case PluginFormat::Standalone: return programFiles + "\\The Usual Suspects";

		default:                  return {};
		}
#elif JUCE_MAC
		// The machine-wide plug-in folders, which is where the installers of nearly every other maker
		// put theirs and so where people go looking (BUG-10526). Writing there may need a password,
		// which the install asks for - see ElevatedTask.
		const std::string library = "/Library/Audio/Plug-Ins";

		switch (_format)
		{
		case PluginFormat::VST2:  return library + "/VST";
		case PluginFormat::VST3:  return library + "/VST3";
		case PluginFormat::CLAP:  return library + "/CLAP";
		case PluginFormat::AU:    return library + "/Components";
		case PluginFormat::LV2:   return library + "/LV2";

		// The user's own Applications folder: the Finder shows it just the same, and a program is
		// only found again by the record of where it went, so moving this default would lose every
		// one installed so far.
		case PluginFormat::Standalone:
			return juce::File::getSpecialLocation(juce::File::userHomeDirectory).getFullPathName().toStdString() + "/Applications";

		default:                  return {};
		}
#elif JUCE_LINUX
		const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getFullPathName().toStdString();

		switch (_format)
		{
		case PluginFormat::VST2:  return home + "/.vst";
		case PluginFormat::VST3:  return home + "/.vst3";
		case PluginFormat::CLAP:  return home + "/.clap";
		case PluginFormat::LV2:   return home + "/.lv2";

		// On the PATH of most distributions, and needing no root. The deb and the rpm put it
		// in /usr/local/bin instead, which is their business rather than this one's.
		case PluginFormat::Standalone: return home + "/.local/bin";

		default:                  return {};
		}
#else
		return {};
#endif
	}

	namespace
	{
		std::map<PluginFormat, std::vector<std::string>> g_searchPaths;
	}

	void PlatformUtils::setSearchPaths(const PluginFormat _format, std::vector<std::string> _paths)
	{
		if (_paths.empty())
			g_searchPaths.erase(_format);
		else
			g_searchPaths[_format] = std::move(_paths);
	}

	std::vector<std::string> PlatformUtils::getSearchPaths(const PluginFormat _format)
	{
		const auto it = g_searchPaths.find(_format);

		if (it != g_searchPaths.end())
			return it->second;

		// A program is not looked for. There is no convention for where one goes, each gets a folder
		// of its own under the location in use, and what is in it is not named after it - so the only
		// thing that can find one is the record of the install that made it.
		if (_format == PluginFormat::Standalone)
			return {};

		std::vector<std::string> paths;

		const auto add = [&paths](std::string _path)
		{
			if (!_path.empty() && std::find(paths.begin(), paths.end(), _path) == paths.end())
				paths.push_back(std::move(_path));
		};

		// Where this would install it, before anywhere else: a copy in that folder is the copy this
		// is about, whatever else is lying around. It is in the list rather than only in front of it
		// because somebody who points a format somewhere of their own still has their old build
		// sitting in the standard folder, and that build is what an upgrade is being offered for.
		add(getDefaultPluginPath(_format));

#if JUCE_WINDOWS
		const auto programFiles = juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory).getFullPathName().toStdString();
		const auto commonFiles = programFiles + "\\Common Files";
		const auto localAppData = juce::File::getSpecialLocation(juce::File::windowsLocalAppData).getFullPathName().toStdString();
		const auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getFullPathName().toStdString();

		switch (_format)
		{
		case PluginFormat::VST2:
			// No convention at all for this one, so these are the folders hosts scan without being told.
			add(programFiles + "\\Steinberg\\VSTPlugins");
			add(commonFiles + "\\VST2");
			add(commonFiles + "\\Steinberg\\VST2");
			break;

		// The per-user folders these formats define beside the machine-wide ones above.
		case PluginFormat::VST3: add(localAppData + "\\Programs\\Common\\VST3"); break;
		case PluginFormat::CLAP: add(localAppData + "\\Programs\\Common\\CLAP"); break;
		case PluginFormat::LV2:  add(appData + "\\LV2"); break;
		default: break;
		}
#elif JUCE_MAC
		// The user's own tree beside the machine-wide default. It was the default until 2.2.28, so
		// everything this installed before then is in there - and found there, it is upgraded where
		// it is rather than installed a second time into the new default (see PluginManager::pathFor).
		const auto library = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getFullPathName().toStdString()
			+ "/Library/Audio/Plug-Ins";

		switch (_format)
		{
		case PluginFormat::VST2: add(library + "/VST"); break;
		case PluginFormat::VST3: add(library + "/VST3"); break;
		case PluginFormat::CLAP: add(library + "/CLAP"); break;
		case PluginFormat::AU:   add(library + "/Components"); break;
		case PluginFormat::LV2:  add(library + "/LV2"); break;
		default: break;
		}
#elif JUCE_LINUX
		// What the VST3 and CLAP specifications name, and where a distribution package puts an LV2.
		switch (_format)
		{
		case PluginFormat::VST2: add("/usr/lib/vst");  add("/usr/local/lib/vst");  break;
		case PluginFormat::VST3: add("/usr/lib/vst3"); add("/usr/local/lib/vst3"); break;
		case PluginFormat::CLAP: add("/usr/lib/clap"); add("/usr/local/lib/clap"); break;
		case PluginFormat::LV2:  add("/usr/lib/lv2");  add("/usr/local/lib/lv2");  break;
		default: break;
		}
#endif

		return paths;
	}

	std::vector<PluginFormat> PlatformUtils::getAvailableFormats()
	{
		std::vector<PluginFormat> formats;

#if JUCE_WINDOWS
		formats = {PluginFormat::VST3, PluginFormat::CLAP, PluginFormat::LV2};
#elif JUCE_MAC
		formats = {PluginFormat::AU, PluginFormat::VST3, PluginFormat::CLAP, PluginFormat::LV2};
#elif JUCE_LINUX
		formats = {PluginFormat::VST3, PluginFormat::CLAP, PluginFormat::LV2};
#endif

		return formats;
	}

	std::string PlatformUtils::formatToString(PluginFormat _format)
	{
		switch (_format)
		{
		case PluginFormat::VST2: return "VST2";
		case PluginFormat::VST3: return "VST3";
		case PluginFormat::CLAP: return "CLAP";
		case PluginFormat::LV2:  return "LV2";
		case PluginFormat::AU:   return "AU";
		case PluginFormat::Standalone: return "Standalone";
		default:                 return "Unknown";
		}
	}

	PluginFormat PlatformUtils::formatFromString(const std::string& _format)
	{
		if (_format == "VST2") return PluginFormat::VST2;
		if (_format == "VST3") return PluginFormat::VST3;
		if (_format == "CLAP") return PluginFormat::CLAP;
		if (_format == "LV2")  return PluginFormat::LV2;
		if (_format == "AU")   return PluginFormat::AU;
		if (_format == "Standalone") return PluginFormat::Standalone;

		return PluginFormat::Count;
	}

	namespace
	{
		std::string g_settingsFolder;
	}

	void PlatformUtils::setSettingsFolder(const std::string& _folder)
	{
		g_settingsFolder = _folder;
	}

	std::string PlatformUtils::dataFolder(const std::string& _product)
	{
		// The same escape hatch the plugin has, so that a machine using it keeps one answer rather
		// than two.
		const auto* overrideFolder = std::getenv("TUS_DATA_FOLDER");

		const auto root = overrideFolder && *overrideFolder
			? baseLib::filesystem::validatePath(overrideFolder)
			: baseLib::filesystem::getSpecialFolderPath(baseLib::filesystem::SpecialFolderType::UserDocuments);

		return baseLib::filesystem::validatePath(root + "The Usual Suspects/" + _product + "/");
	}

	std::string PlatformUtils::romFolder(const std::string& _product)
	{
		return baseLib::filesystem::validatePath(dataFolder(_product) + "roms/");
	}

	std::string PlatformUtils::settingsFolder()
	{
		if (!g_settingsFolder.empty())
			return baseLib::filesystem::validatePath(g_settingsFolder);

		// Where the rest of the family keeps its things: Documents/The Usual Suspects/<product>/.
		// Through baseLib rather than JUCE's own userDocumentsDirectory, because the two disagree
		// on Linux - JUCE answers ~/Documents, baseLib follows the XDG spec - and the point of this
		// is to land in the same folder the plugins do.
		return baseLib::filesystem::validatePath(
			baseLib::filesystem::getSpecialFolderPath(baseLib::filesystem::SpecialFolderType::UserDocuments) +
			"The Usual Suspects/PluginManager/");
	}

	void PlatformUtils::restrictToOwner([[maybe_unused]] const std::string& _path)
	{
#if !JUCE_WINDOWS
		if (!_path.empty())
			chmod(_path.c_str(), S_IRUSR | S_IWUSR);
#endif
	}

	std::string PlatformUtils::getSettingsPath(const std::string& _fileName)
	{
		// config/ beneath it, beside the roms/ and patchmanager/ folders a plugin puts there - but
		// not when a folder was named on the command line, where what was named is the folder and
		// a level beneath it would be a surprise.
		const auto dir = g_settingsFolder.empty()
			? baseLib::filesystem::validatePath(settingsFolder() + "config/")
			: settingsFolder();

		return juce::File(juce::String(dir)).getChildFile(juce::String(_fileName))
			.getFullPathName().toStdString();
	}

	std::string PlatformUtils::pluginFileName(const std::string& _pluginId, const PluginFormat _format)
	{
		switch (_format)
		{
		case PluginFormat::VST2:
#if JUCE_WINDOWS
			return _pluginId + ".dll";
#elif JUCE_MAC
			return _pluginId + ".vst";
#else
			return _pluginId + ".so";
#endif
		case PluginFormat::VST3:	return _pluginId + ".vst3";
		case PluginFormat::CLAP:	return _pluginId + ".clap";
		case PluginFormat::LV2:		return _pluginId + ".lv2";
		case PluginFormat::AU:		return _pluginId + ".component";
		case PluginFormat::Standalone:
			// A program rather than a plugin, and the archive holds more than it - but this is the
			// part that says whether it is installed, and the part an uninstall removes.
#if JUCE_WINDOWS
			return _pluginId + ".exe";
#elif JUCE_MAC
			return _pluginId + ".app";
#else
			return _pluginId;
#endif
		default:					return {};
		}
	}

	std::string PlatformUtils::installedFilePath(const std::string& _pluginId, const PluginFormat _format,
	                                             const std::string& _dir)
	{
		const auto name = pluginFileName(_pluginId, _format);

		if (name.empty() || _dir.empty())
			return {};

		return juce::File(juce::String(_dir)).getChildFile(juce::String(name)).getFullPathName().toStdString();
	}

	bool PlatformUtils::isPluginInstalled(const std::string& _pluginId, const PluginFormat _format, const std::string& _dir)
	{
		const auto path = installedFilePath(_pluginId, _format, _dir);

		return !path.empty() && juce::File(juce::String(path)).exists();
	}

	namespace
	{
		// An LV2 is a folder holding a bare shared library and the turtle files that describe it.
		// The turtle is where the version is: doap:revision carries the one the build was made from.
		//
		// Read on every system rather than the library beside it, because on two of the three there is
		// nothing in that library to read: macOS asks CFBundle, which wants a bundle with an
		// Info.plist and an LV2 is not one, and an ELF carries nothing at all. Windows could read its
		// resource and gets the same answer, so one way of asking serves all three.
		std::string lv2RevisionOf(const juce::File& _bundle)
		{
			if (!_bundle.isDirectory())
				return {};

			// Whichever of them says it. Every file rather than the one named in the manifest, since
			// the point is to find a line, and a turtle naming no revision costs a read and nothing else.
			for (const auto& ttl : _bundle.findChildFiles(juce::File::findFiles, false, "*.ttl"))
			{
				// In one read. These run to megabytes - 17 MB for OsTIrus, with the revision near the end
				// of it - and reading them a line at a time from an unbuffered stream was one system call
				// per byte: the better part of a minute per plugin, on the thread that draws the list, which
				// is what kept the window "Not Responding" for so long after it opened (EMU-222).
				juce::MemoryBlock data;

				if (!ttl.loadFileAsData(data))
					continue;

				const std::string_view text(static_cast<const char*>(data.getData()), data.getSize());

				// doap:revision "2.2.25" ;
				for (auto at = text.find("doap:revision"); at != std::string_view::npos; at = text.find("doap:revision", at + 1))
				{
					const auto open = text.find('"', at);
					const auto close = open == std::string_view::npos ? open : text.find('"', open + 1);

					// On the same line, as before: a quote further down belongs to something else.
					if (close == std::string_view::npos || close == open + 1 ||
						text.substr(at, close - at).find('\n') != std::string_view::npos)
					{
						continue;
					}

					return std::string(text.substr(open + 1, close - open - 1));
				}
			}

			return {};
		}

#if JUCE_WINDOWS
		// A VST3 or a CLAP keeps its binary under Contents, named after the bundle itself:
		//     Osirus.vst3/Contents/x86_64-win/Osirus.vst3
		//
		// A CLAP is a plain file and is its own binary. An LV2 never reaches here - see above.
		juce::File binaryOf(const juce::File& _installed)
		{
			if (!_installed.isDirectory())
				return _installed;

			const auto contents = _installed.getChildFile("Contents");

			if (contents.isDirectory())
			{
				for (const auto& file : contents.findChildFiles(juce::File::findFiles, true))
				{
					if (file.getFileName() == _installed.getFileName())
						return file;
				}

				return {};
			}

			return {};
		}

		std::vector<char> versionInfoOf(const juce::File& _file)
		{
			if (!_file.existsAsFile())
				return {};

			const auto path = _file.getFullPathName();

			DWORD handle = 0;
			const auto size = GetFileVersionInfoSizeW(path.toWideCharPointer(), &handle);

			if (!size)
				return {};

			std::vector<char> data(size);

			if (!GetFileVersionInfoW(path.toWideCharPointer(), handle, size, data.data()))
				return {};

			return data;
		}

		std::string versionResourceOf(const juce::File& _file)
		{
			auto data = versionInfoOf(_file);

			if (data.empty())
				return {};

			VS_FIXEDFILEINFO* info = nullptr;
			UINT length = 0;

			// The fixed part, rather than a string table: no language and code page to guess at.
			if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info || !length)
				return {};

			const auto major = HIWORD(info->dwFileVersionMS);
			const auto minor = LOWORD(info->dwFileVersionMS);
			const auto patch = HIWORD(info->dwFileVersionLS);

			if (!major && !minor && !patch)
				return {};

			return std::to_string(major) + '.' + std::to_string(minor) + '.' + std::to_string(patch);
		}

		// From the string table this time, which is per language: the first one the resource lists.
		std::string companyOf(const juce::File& _file)
		{
			auto data = versionInfoOf(_file);

			if (data.empty())
				return {};

			struct Translation { WORD language; WORD codePage; };

			Translation* translations = nullptr;
			UINT length = 0;

			if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &length) ||
				!translations || length < sizeof(Translation))
			{
				return {};
			}

			wchar_t key[64];
			swprintf(key, 64, L"\\StringFileInfo\\%04x%04x\\CompanyName", translations->language, translations->codePage);

			wchar_t* company = nullptr;

			if (!VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&company), &length) || !company || !length)
				return {};

			return juce::String(company).trim().toStdString();
		}
#elif JUCE_MAC
		// Every plugin format is a bundle here, so the version is in its Info.plist. Read through
		// CFBundle rather than as XML: Xcode writes those plists in the binary format.
		std::string bundleStringOf(const juce::File& _bundle, const CFStringRef _key)
		{
			if (!_bundle.isDirectory())
				return {};

			const auto path = _bundle.getFullPathName();

			auto* cfPath = CFStringCreateWithCString(nullptr, path.toRawUTF8(), kCFStringEncodingUTF8);

			if (!cfPath)
				return {};

			auto* url = CFURLCreateWithFileSystemPath(nullptr, cfPath, kCFURLPOSIXPathStyle, true);
			CFRelease(cfPath);

			if (!url)
				return {};

			auto* bundle = CFBundleCreate(nullptr, url);
			CFRelease(url);

			if (!bundle)
				return {};

			std::string result;

			const auto* value = CFBundleGetValueForInfoDictionaryKey(bundle, _key);

			if (value && CFGetTypeID(value) == CFStringGetTypeID())
			{
				char buffer[256] = {};

				if (CFStringGetCString(static_cast<CFStringRef>(value), buffer, sizeof(buffer), kCFStringEncodingUTF8))
					result = buffer;
			}

			CFRelease(bundle);

			return result;
		}
#endif
	}

	std::string PlatformUtils::getInstalledVersion(const std::string& _pluginId, const PluginFormat _format,
	                                               const std::string& _dir)
	{
		const auto path = installedFilePath(_pluginId, _format, _dir);

		if (path.empty())
			return {};

		const auto installed = juce::File(juce::String(path));

		if (!installed.exists())
			return {};

		// Before the rest, and the same answer everywhere: the turtle in an LV2 is the only place
		// that says which version it is.
		if (_format == PluginFormat::LV2)
			return lv2RevisionOf(installed);

#if JUCE_WINDOWS
		return versionResourceOf(binaryOf(installed));
#elif JUCE_MAC
		// The short string is the one that reads like a version; CFBundleVersion is the build.
		return bundleStringOf(installed, CFSTR("CFBundleShortVersionString"));
#else
		// An ELF carries nothing to read, so for everything but an LV2 a plugin this program did not
		// install is of an unknown version here.
		return {};
#endif
	}

	std::string PlatformUtils::otherMaker(const std::string& _path)
	{
		const auto file = juce::File(juce::String(_path));

		if (!file.exists())
			return {};

#if JUCE_MAC
		const auto id = bundleStringOf(file, CFSTR("CFBundleIdentifier"));

		// Builds from before BUNDLE_ID was set carry JUCE's default, the company name as it is
		// written: "com.The Usual Suspects.<target>". Compared without what is not a letter, a digit
		// or a dot, so that one and ours are the same name.
		const auto normalised = juce::String(id).toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789.");

		return id.empty() || normalised.startsWith("com.theusualsuspects.") ? std::string() : id;
#elif JUCE_WINDOWS
		const auto company = companyOf(binaryOf(file));

		return company.empty() || juce::String(company).equalsIgnoreCase("The Usual Suspects") ? std::string() : company;
#else
		return {};
#endif
	}
}
