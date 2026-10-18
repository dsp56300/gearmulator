#include "asset.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

namespace pluginManager
{
	namespace
	{
		constexpr char g_namePrefix[] = "TheUsualSuspects-";

		bool isKnownFormat(const std::string& _s)
		{
			// Standalone only ever shows up in internal builds, never in a public release, which
			// is why the download page on the website does not list it.
			return _s == "VST2" || _s == "VST3" || _s == "CLAP" || _s == "LV2" || _s == "AU" || _s == "Standalone";
		}

		bool isKnownPackage(const std::string& _s)
		{
			return _s == "zip" || _s == "deb" || _s == "rpm";
		}

		std::vector<std::string> split(const std::string& _s, const char _separator)
		{
			std::vector<std::string> result;

			size_t begin = 0;

			while (true)
			{
				const auto pos = _s.find(_separator, begin);

				if (pos == std::string::npos)
				{
					result.push_back(_s.substr(begin));
					return result;
				}

				result.push_back(_s.substr(begin, pos - begin));
				begin = pos + 1;
			}
		}

		// win64 | MacOS | Linux_x86_64 | Linux_aarch64
		bool parsePlatform(const std::string& _token, std::string& _os, std::string& _arch)
		{
			if (_token == "win64")
			{
				_os = "windows";
				_arch = "x86_64";
				return true;
			}

			if (_token == "MacOS")
			{
				_os = "macos";
				_arch = "universal";
				return true;
			}

			const auto separator = _token.find('_');

			if (separator == std::string::npos || _token.substr(0, separator) != "Linux")
				return false;

			_os = "linux";
			_arch = _token.substr(separator + 1);

			return !_arch.empty();
		}
	}

	bool parseAssetName(const std::string& _fileName, Asset& _asset)
	{
		constexpr size_t prefixLen = sizeof(g_namePrefix) - 1;

		if (_fileName.size() <= prefixLen || _fileName.compare(0, prefixLen, g_namePrefix) != 0)
			return false;

		// The prefix holds no dot and the version does, so the last dot is always the extension.
		const auto dot = _fileName.rfind('.');

		if (dot == std::string::npos)
			return false;

		const auto package = _fileName.substr(dot + 1);

		if (!isKnownPackage(package))
			return false;

		const auto tokens = split(_fileName.substr(prefixLen, dot - prefixLen), '-');

		// Parsed from the right, because the product name is the only part that may itself contain
		// dashes: Synth-1000, Rack-A-100. Anchoring on the left splits those down the middle.
		if (tokens.size() < 3)
			return false;

		const auto& platform = tokens.back();
		const auto& version = tokens[tokens.size() - 2];

		// The version is what tells product apart from format: everything left of it is the name.
		if (version.empty() || !std::isdigit(static_cast<unsigned char>(version[0])))
			return false;

		auto productEnd = tokens.size() - 2;

		// No format segment means this is not a plugin but a program. Standalone is what that is
		// here, and saying so is what makes those builds installable rather than merely listed: a
		// format has an install location, a file name and an uninstall, and no format has none of
		// the three.
		std::string format = "Standalone";

		if (tokens.size() >= 4 && isKnownFormat(tokens[tokens.size() - 3]))
		{
			format = tokens[tokens.size() - 3];
			productEnd = tokens.size() - 3;
		}

		std::string product = tokens[0];

		for (size_t i = 1; i < productEnd; ++i)
			product += '-' + tokens[i];

		if (product.empty())
			return false;

		std::string os;
		std::string arch;

		if (!parsePlatform(platform, os, arch))
			return false;

		_asset.product = product;
		_asset.format = format;
		_asset.version = version;
		_asset.os = os;
		_asset.arch = arch;
		_asset.package = package;

		return true;
	}

	int compareVersions(const std::string& _a, const std::string& _b)
	{
		const auto a = split(_a, '.');
		const auto b = split(_b, '.');

		const auto count = std::max(a.size(), b.size());

		for (size_t i = 0; i < count; ++i)
		{
			// A missing component counts as zero so that 2.2 and 2.2.0 compare equal.
			const auto va = i < a.size() ? std::strtol(a[i].c_str(), nullptr, 10) : 0;
			const auto vb = i < b.size() ? std::strtol(b[i].c_str(), nullptr, 10) : 0;

			if (va != vb)
				return va < vb ? -1 : 1;
		}

		return 0;
	}

	InstallAction combineInstallActions(const InstallAction _plugin, const InstallAction _fx)
	{
		for (const auto action : {InstallAction::Install, InstallAction::Upgrade, InstallAction::Downgrade})
		{
			if (_plugin == action || _fx == action)
				return action;
		}

		return InstallAction::Reinstall;
	}

	// A build that cannot say which version it is offers a reinstall rather than an upgrade that
	// cannot be promised.
	InstallAction decideInstallAction(const bool _installed, const std::string& _installedVersion,
	                                  const std::string& _offered)
	{
		if (!_installed)
			return InstallAction::Install;

		if (_installedVersion.empty() || _offered.empty())
			return InstallAction::Reinstall;

		const auto difference = compareVersions(_installedVersion, _offered);

		if (difference < 0)
			return InstallAction::Upgrade;

		return difference > 0 ? InstallAction::Downgrade : InstallAction::Reinstall;
	}
}
