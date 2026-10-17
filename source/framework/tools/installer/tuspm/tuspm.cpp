// The headless plugin manager: the same library the GUI runs on, driven from a script.

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "pluginManagerLib/config.h"
#include "pluginManagerLib/elevatedTask.h"
#include "pluginManagerLib/installPaths.h"
#include "pluginManagerLib/log.h"
#include "pluginManagerLib/pluginInstaller.h"
#include "pluginManagerLib/loginFile.h"
#include "pluginManagerLib/pluginManager.h"

using namespace pluginManager;

namespace
{
	struct Args
	{
		std::string command;
		std::vector<std::string> positional;
		std::map<std::string, std::string> options;

		bool has(const std::string& _name) const { return options.find(_name) != options.end(); }

		std::string get(const std::string& _name, const std::string& _default = {}) const
		{
			const auto it = options.find(_name);
			return it == options.end() ? _default : it->second;
		}

		const std::string& positionalAt(const size_t _index) const
		{
			static const std::string empty;
			return _index < positional.size() ? positional[_index] : empty;
		}
	};

	// --name value and --name=value, everything else is positional. The first positional is the command.
	bool parseArgs(const int _argc, char** _argv, Args& _args, std::string& _error)
	{
		for (int i = 1; i < _argc; ++i)
		{
			std::string arg = _argv[i];

			if (arg.compare(0, 2, "--") != 0)
			{
				_args.positional.push_back(arg);
				continue;
			}

			auto name = arg.substr(2);
			std::string value;

			const auto eq = name.find('=');

			if (eq != std::string::npos)
			{
				value = name.substr(eq + 1);
				name = name.substr(0, eq);
			}
			else if (i + 1 < _argc && std::string(_argv[i + 1]).compare(0, 2, "--") != 0)
			{
				value = _argv[++i];
			}
			else
			{
				_error = "Missing value for --" + name;
				return false;
			}

			_args.options[name] = value;
		}

		if (_args.positional.empty())
		{
			_error = "No command given";
			return false;
		}

		_args.command = _args.positional.front();
		_args.positional.erase(_args.positional.begin());

		return true;
	}

	const char* channelName(const Channel _channel)
	{
		switch (_channel)
		{
		case Channel::Internal: return "internal";
		case Channel::Donators: return "donators";
		default:                return "public";
		}
	}

	bool parseChannel(const std::string& _name, Channel& _channel)
	{
		if (_name == "internal")
			_channel = Channel::Internal;
		else if (_name == "donators")
			_channel = Channel::Donators;
		else
			return false;

		return true;
	}

	// A login handed to this run rather than taken from the settings. It is not stored: a script on a
	// build machine should not leave the password behind.
	bool applyCommandLineLogin(PluginManager& _pluginManager, const Args& _args, std::string& _error)
	{
		if (!_args.has("user") && !_args.has("password"))
			return true;

		// For login they are the login itself. Read here as well, with no --channel, they became the
		// team login for this run - and the login command then saves the whole store, so `login
		// donators <product>` wrote the donator password over the team login in the settings.
		if (_args.command == "login")
			return true;

		const Credentials credentials{_args.get("user"), _args.get("password")};

		if (!credentials.valid())
		{
			_error = "--user and --password go together";
			return false;
		}

		Channel channel = Channel::Internal;
		const auto channelArg = _args.get("channel", "internal");

		if (!parseChannel(channelArg, channel))
		{
			_error = "Unknown channel: " + channelArg;
			return false;
		}

		_pluginManager.setCredentials(channel, _args.get("product"), credentials);

		return true;
	}

	// Refreshing is what fills the catalog, so every command but a login starts here. A channel that
	// could not be read is reported but does not stop the run: the rest of the catalog still works.
	void refresh(PluginManager& _pluginManager)
	{
		std::string error;

		if (!_pluginManager.refresh(error))
			std::cerr << "warning: " << error << '\n';
	}

	int list(PluginManager& _pluginManager, const Args& _args)
	{
		refresh(_pluginManager);

		const auto& catalog = _pluginManager.getCatalog();
		const auto os = PlatformUtils::getCurrentOS();
		const auto arch = PlatformUtils::getCurrentArch();

		const auto& wanted = _args.positionalAt(0);

		// Everything installable by name, which is more than the products the window shows: an
		// effect build ships under a name of its own, install takes that name, and a list that
		// leaves it out is one a script cannot work from. A variant with no build for this
		// platform - a test console has none - simply contributes no lines.
		std::vector<std::string> products;

		for (const auto& product : catalog.getProducts())
		{
			products.push_back(product.product);

			for (auto& variant : catalog.getVariants(product.product))
				products.push_back(std::move(variant));
		}

		// A product whose name is the start of another one would otherwise be listed once for
		// itself and again as the other's variant.
		std::sort(products.begin(), products.end());
		products.erase(std::unique(products.begin(), products.end()), products.end());

		// Tab separated, one build per line, so that a script can cut it up.
		size_t lines = 0;

		for (const auto& product : products)
		{
			if (!wanted.empty() && product != wanted)
				continue;

			for (const auto& format : catalog.getFormats(product, os, arch))
			{
				const auto installedVersion = _pluginManager.getInstalledVersion(product, format);

				for (const auto& version : catalog.getVersions(product, format, os, arch))
				{
					const auto channel = catalog.getChannel(product, format, version, os, arch);

					// The last column is what is on disk for this format, which is the same on every
					// line of a format - a script can tell what it has by comparing the two.
					std::cout << product << '\t' << format << '\t' << version << '\t' << channelName(channel)
					          << '\t' << installedVersion << '\n';
					++lines;
				}
			}
		}

		if (!lines)
		{
			std::cerr << (wanted.empty() ? "Nothing available for this platform" : "Nothing available for " + wanted) << '\n';
			return 1;
		}

		return 0;
	}

	int install(PluginManager& _pluginManager, const Args& _args)
	{
		const auto& product = _args.positionalAt(0);

		if (product.empty())
		{
			std::cerr << "install needs a product\n";
			return 1;
		}

		refresh(_pluginManager);

		const auto& catalog = _pluginManager.getCatalog();
		const auto os = PlatformUtils::getCurrentOS();
		const auto arch = PlatformUtils::getCurrentArch();

		// No --format installs every format there is for this platform, which is what the GUI offers
		// with all of its boxes ticked.
		auto formats = catalog.getFormats(product, os, arch);

		if (_args.has("format"))
			formats = {_args.get("format")};

		if (formats.empty())
		{
			std::cerr << "Nothing available for " << product << " on this platform\n";
			return 1;
		}

		auto failed = false;

		for (const auto& format : formats)
		{
			const auto versions = catalog.getVersions(product, format, os, arch);

			// Newest first, so an unspecified version is the newest one.
			const auto version = _args.get("version", versions.empty() ? std::string() : versions.front());

			const auto* asset = catalog.find(product, format, version, os, arch);

			if (!asset)
			{
				// No --version given means the newest, so there is nothing to name in that case.
				std::cerr << "No build of " << product << " " << format
					<< (version.empty() ? std::string() : " " + version) << " for this platform\n";
				failed = true;
				continue;
			}

			const auto targetDir = _pluginManager.installPathFor(*asset);

			std::cout << "Installing " << product << " " << format << " " << asset->version
			          << " (" << channelName(asset->channel) << ") to " << targetDir << std::endl;

			PluginInstaller installer;
			std::string message;

			if (!installer.install(*asset, _pluginManager.credentialsFor(*asset), targetDir, {}, message))
			{
				std::cerr << message << '\n';
				failed = true;
				continue;
			}

			// Written down so that a later run - and the GUI - know which version is on disk, and what
			// an uninstall has to take away besides the plugin.
			_pluginManager.recordInstall(*asset, installer.lastInstalledFiles());

			std::cout << message << '\n';

			std::string filesMessage;

			if (!_pluginManager.installAdditionalFiles(*asset, filesMessage))
			{
				std::cerr << filesMessage << '\n';
				failed = true;
			}
			else if (!filesMessage.empty())
			{
				std::cout << filesMessage << '\n';
			}
		}

		return failed ? 1 : 0;
	}

	// One format, or everything that is installed for the product when none is named.
	int uninstall(PluginManager& _pluginManager, const Args& _args)
	{
		const auto& product = _args.positionalAt(0);

		if (product.empty())
		{
			std::cerr << "uninstall needs a product\n";
			return 1;
		}

		// No refresh: what is installed is on this disk, and saying what can be removed must work
		// without the network.
		std::vector<std::string> formats;

		if (_args.has("format"))
		{
			formats.push_back(_args.get("format"));
		}
		else
		{
			for (const auto format : InstallPaths::getFormats())
			{
				if (_pluginManager.isInstalled(product, PlatformUtils::formatToString(format)))
					formats.push_back(PlatformUtils::formatToString(format));
			}
		}

		if (formats.empty())
		{
			std::cerr << "Nothing of " << product << " is installed here\n";
			return 1;
		}

		// Every format in one call, so a system folder asks for administrator rights once.
		std::vector<std::pair<std::string, std::string>> items;

		for (const auto& format : formats)
			items.emplace_back(product, format);

		std::string failure;

		const auto results = _pluginManager.uninstallMany(items, failure);

		for (size_t i = 0; i < results.size(); ++i)
		{
			if (results[i])
				std::cout << product << ' ' << formats[i] << " removed\n";
		}

		if (failure.empty())
			return 0;

		std::cerr << failure << '\n';
		return 1;
	}

	// Hand-editing this was the trap it exists to replace: a settings file whose paths are all
	// default is written as <pluginManager/>, so there is no block to put anything inside, and an
	// element put at the top level instead is read by nobody and says so to nobody.
	int teamLogin(PluginManager& _pluginManager, const Args& _args)
	{
		auto& installPaths = _pluginManager.getInstallPaths();

		const auto& state = _args.positionalAt(0);

		if (!state.empty())
		{
			if (state != "on" && state != "off")
			{
				std::cerr << "teamlogin takes on or off, or nothing to say which it is\n";
				return 1;
			}

			installPaths.setTeamLogin(state == "on");
			installPaths.save();
		}

		std::cout << (installPaths.hasTeamLogin() ? "on" : "off") << '\n';

		return 0;
	}

	// No arguments lists what is set, a format and a location changes it. "default" puts a format back
	// to the location the platform expects.
	int paths(PluginManager& _pluginManager, const Args& _args)
	{
		auto& installPaths = _pluginManager.getInstallPaths();

		const auto& format = _args.positionalAt(0);

		if (!format.empty())
		{
			const auto pluginFormat = PlatformUtils::formatFromString(format);

			if (pluginFormat == PluginFormat::Count)
			{
				std::cerr << "Unknown format: " << format << '\n';
				return 1;
			}

			const auto& location = _args.positionalAt(1);

			if (location.empty())
			{
				std::cerr << "paths " << format << " needs a location, or \"default\"\n";
				return 1;
			}

			installPaths.set(pluginFormat, location == "default" ? std::string() : location);
			installPaths.save();
		}

		for (const auto f : InstallPaths::getFormats())
		{
			const auto path = installPaths.get(f);

			std::cout << PlatformUtils::formatToString(f) << '\t'
			          << (path.empty() ? "<none>" : path) << '\t'
			          << (installPaths.isCustom(f) ? "custom" : "default") << '\n';
		}

		return 0;
	}

	int login(PluginManager& _pluginManager, const Args& _args)
	{
		Channel channel = Channel::Internal;

		if (!parseChannel(_args.positionalAt(0), channel))
		{
			std::cerr << "login needs a channel: internal or donators\n";
			return 1;
		}

		// A donator login belongs to one product, the team login to all of them.
		const auto slug = channel == Channel::Donators ? _args.positionalAt(1) : std::string();

		if (channel == Channel::Donators && slug.empty())
		{
			std::cerr << "A donator login needs the product it is for\n";
			return 1;
		}

		const Credentials credentials{_args.get("user"), _args.get("password")};

		if (!credentials.valid())
		{
			std::cerr << "login needs --user and --password\n";
			return 1;
		}

		std::string error;

		if (!_pluginManager.login(channel, slug, credentials, error))
		{
			std::cerr << error << '\n';
			return 1;
		}

		std::cout << "Login stored\n";

		return 0;
	}

	int logout(PluginManager& _pluginManager, const Args& _args)
	{
		Channel channel = Channel::Internal;

		if (!parseChannel(_args.positionalAt(0), channel))
		{
			std::cerr << "logout needs a channel: internal or donators\n";
			return 1;
		}

		_pluginManager.logout(channel, channel == Channel::Donators ? _args.positionalAt(1) : std::string());

		return 0;
	}

	// What a command produced, to the file --out names, or printed when it names none.
	int output(const std::string& _text, const std::string& _out)
	{
		if (_out.empty())
		{
			std::cout << _text << std::endl;
			return 0;
		}

		std::ofstream file(_out, std::ios::binary | std::ios::trunc);

		if (!file.is_open())
		{
			std::cerr << "Could not write " << _out << "\n";
			return 1;
		}

		file << _text;
		file.close();

		std::cout << "Written to " << _out << std::endl;

		return 0;
	}

	// Turns logins this machine has into one line to send, so that somebody being given a build to
	// try drops a file or pastes a line instead of being told a folder, a user and a password.
	int share(PluginManager& _pluginManager, const Args& _args)
	{
		std::vector<SharedLogin> logins;

		// Named rather than all of them: handing over every folder this machine can reach because
		// the slug was forgotten is not a mistake worth leaving available.
		for (size_t i = 0;; ++i)
		{
			const auto& slug = _args.positionalAt(i);

			if (slug.empty())
				break;

			const auto credentials = _pluginManager.getLogin(Channel::Donators, slug);

			if (!credentials.valid())
			{
				std::cerr << "No donator login stored for " << slug << "\n";
				return 1;
			}

			logins.push_back({slug, credentials});
		}

		if (logins.empty())
		{
			std::cerr << "share needs at least one product folder\n";
			return 1;
		}

		return output(LoginFile::write(logins), _args.get("out"));
	}

	// A product's changelog, as the GUI shows it: from the newest place the logins here can read, the
	// public one ending at the newest release for anybody without the team login. Every version,
	// newest first, or the one --version names; an entry to a line as text, or as Markdown.
	int changelog(PluginManager& _pluginManager, const Args& _args)
	{
		const auto& product = _args.positionalAt(0);

		if (product.empty())
		{
			std::cerr << "changelog needs a product\n";
			return 1;
		}

		const auto format = _args.get("format", "text");

		if (format != "text" && format != "markdown")
		{
			std::cerr << "--format is text or markdown\n";
			return 1;
		}

		// Which places there are to read, and where the public one ends, is in the catalog.
		refresh(_pluginManager);

		changelogLib::Changelog log;
		std::string error;

		if (!_pluginManager.fetchChangelog(product, log, error))
		{
			std::cerr << error << '\n';
			return 1;
		}

		const auto section = log.findProduct(product);
		const auto& wanted = _args.get("version");

		std::string text;

		for (const auto& release : log.getReleases(section))
		{
			if (release.version.empty())
				continue;

			// By number, so that 2.2.16 finds a heading that carries a date after it.
			if (!wanted.empty() && changelogLib::Changelog::versionToInt(release.version) != changelogLib::Changelog::versionToInt(wanted))
				continue;

			if (!text.empty())
				text += '\n';

			text += format == "markdown"
				? changelogLib::Changelog::toMarkdown(release, section)
				: changelogLib::Changelog::toText(release, section);
		}

		if (text.empty())
		{
			std::cerr << (wanted.empty() ? "The changelog has nothing about " + product : "The changelog of " + product + " has no version " + wanted) << '\n';
			return 1;
		}

		return output(text, _args.get("out"));
	}

	void usage()
	{
		std::cout <<
			g_appName << " " << g_appVersion << " (headless)\n"
			"\n"
			"  tuspm list [<product>]\n"
			"  tuspm install <product> [--format <format>] [--version <version>]\n"
			"  tuspm uninstall <product> [--format <format>]\n"
			"  tuspm login <internal|donators> [<product>] --user <user> --password <password>\n"
			"  tuspm logout <internal|donators> [<product>]\n"
			"  tuspm paths [<format> <location>|<format> default]\n"
			"  tuspm changelog <product> [--version <version>] [--format text|markdown] [--out <file>]\n"
			"\n"
			"--settings <folder> works with any of them, and puts the logins, the locations, the\n"
			"record of what is installed and the log somewhere other than where this machine keeps\n"
			"its own.\n"
			"\n"
			"list and install take --user/--password for this run only, with --channel (internal by\n"
			"default) and --product saying which channel the login is for. A login stored by login or\n"
			"by the GUI is used when none is given.\n"
			"\n"
			"list prints one build per line, separated by tabs: product, format, version, channel, and\n"
			"the version of that format that is installed (empty when none is, or when the format\n"
			"cannot say - see the notes in installedPlugins.h).\n";
	}
}

int main(const int _argc, char** _argv)
{
	// Before anything else: this process may have been started by the one the user is running, to
	// write an install into a folder that belongs to the system. Then it does that and nothing else.
	{
		int exitCode = 0;

		if (ElevatedTask::runAsWorker({_argv + 1, _argv + _argc}, exitCode))
			return exitCode;
	}

	Args args;
	std::string error;

	if (!parseArgs(_argc, _argv, args, error))
	{
		if (_argc > 1)
			std::cerr << error << "\n\n";

		usage();
		return _argc > 1 ? 1 : 0;
	}

	if (args.command == "help" || args.has("help"))
	{
		usage();
		return 0;
	}

	// Before a PluginManager exists: the logins, the locations and the record of what is installed
	// all live in that folder, and it is read the moment one is constructed.
	if (args.has("settings"))
		PlatformUtils::setSettingsFolder(args.get("settings"));

	Log::start();

	PluginManager pluginManager;

	if (!applyCommandLineLogin(pluginManager, args, error))
	{
		std::cerr << error << '\n';
		return 1;
	}

	if (args.command == "list")
		return list(pluginManager, args);

	if (args.command == "install")
		return install(pluginManager, args);

	if (args.command == "login")
		return login(pluginManager, args);

	if (args.command == "logout")
		return logout(pluginManager, args);

	if (args.command == "share")
		return share(pluginManager, args);

	if (args.command == "teamlogin")
		return teamLogin(pluginManager, args);

	if (args.command == "paths")
		return paths(pluginManager, args);

	if (args.command == "uninstall")
		return uninstall(pluginManager, args);

	if (args.command == "changelog")
		return changelog(pluginManager, args);

	std::cerr << "Unknown command: " << args.command << "\n\n";
	usage();

	return 1;
}
