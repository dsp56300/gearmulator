#pragma once

#include <cstdint>
#include <string>

namespace pluginManager
{
	enum class Channel : uint8_t
	{
		Public,		// GitHub releases
		Donators,	// <build server>/builds/<slug>/donators, one login per product
		Internal	// <build server>/builds/<slug>/internal, one login for the whole team
	};

	// One downloadable file.
	//
	// Every source names its files the same way - GitHub release assets and the files in a
	// protected channel directory alike - so parsing the file name is enough to describe an
	// asset no matter where it came from. Variants are part of the product name rather than a
	// nested structure: Osirus, OsirusFX, OsirusTestConsole are three products here.
	struct Asset
	{
		std::string product;	// "Osirus", "OsirusFX", "OsirusTestConsole", "DSPBridgeServer"
		std::string format;		// "VST3", ... "Standalone" for a program rather than a plugin
		std::string version;	// "2.2.16"
		std::string os;			// "windows", "macos", "linux"
		std::string arch;		// "x86_64", "aarch64", "universal"
		std::string package;	// "zip", "deb", "rpm"
		std::string url;
		Channel channel = Channel::Public;

		// What the folder listing printed beside the name, "2026-10-05 18:37" and nothing more: the
		// server's clock, in the server's zone. Never parsed into a time and only ever compared with
		// another value that came from a listing, so one string against another says which of two
		// builds is newer without this program having to know where the server lives.
		//
		// A protected channel reuses the version between builds, so the date is the only thing that
		// tells one from the next. A public build has the date its release was published, in the same
		// shape, which is what a copy that cannot say its version is held against - see
		// PluginManager::isOutdated.
		std::string listed;

		// The product whose folder in Documents this build uses. An effect build uses the one of the
		// plugin it extends, and only the product lists can say which builds those are: a build named
		// after its plugin with FX after it looks exactly like a product whose own name ends in FX.
		// Filled in by the catalog - see Catalog::dataProductOf. An asset made anywhere else is taken
		// to use its own.
		std::string dataProduct;

		const std::string& getDataProduct() const { return dataProduct.empty() ? product : dataProduct; }
	};

	// Parses TheUsualSuspects-<Product>[-<FORMAT>]-<version>-<os>[_<arch>].<package>
	//
	// A name with no format segment is a program rather than a plugin - a test console, the bridge
	// server, this installer - and reads as Standalone, which is the one format that means exactly
	// that.
	//
	// The product is the only part that may contain dashes (Synth-1000, Rack-A-100), so the name is
	// read from the right: platform, version, then an optional format, and whatever is left is the
	// product. Reading it from the left splits those names down the middle.
	//
	// Returns false for anything that does not match, so unrelated files in a directory listing
	// are ignored rather than turning into half-filled entries.
	bool parseAssetName(const std::string& _fileName, Asset& _asset);

	// Compares numerically per component, so 2.2.9 sorts before 2.2.16. A plain string compare
	// gets that pair backwards, which is why the version list must not be sorted as text.
	int compareVersions(const std::string& _a, const std::string& _b);

	// What installing the offered version over what is there would do. The button that does it says
	// so - in words on the detail page, as an icon in the list - and both ask here.
	enum class InstallAction : uint8_t
	{
		Install,	// nothing is installed
		Reinstall,	// the same version, or one that does not say what it is
		Upgrade,
		Downgrade	// only reachable where an older version can be picked, which is the list
	};

	InstallAction decideInstallAction(bool _installed, const std::string& _installedVersion,
	                                  const std::string& _offered);

	// What one button offers for a plugin and the effect build that goes with it: the more
	// pressing of the two, so that a plugin which is up to date next to an effect build that is
	// not still offers to fetch it. Something missing comes before something behind, and a
	// reinstall is what is left when there is nothing either of them needs.
	InstallAction combineInstallActions(InstallAction _plugin, InstallAction _fx);

	// Whether installing or upgrading a format of a plugin takes its effect build along: there has to
	// be one, and either it was asked for - the FX chip - or it is installed already. An installed
	// effect build is kept in step with its plugin, asked for or not. Going by the chip alone, which is
	// off until somebody turns it on, left it out of date while "Updates available" counted it: the
	// plugin stayed in that group after an upgrade and its effect build was never upgraded (BUG-10488).
	// Removing goes by the chip alone, so nothing is taken away that was not asked for.
	bool includesFxBuild(bool _offered, bool _wanted, bool _installed);

	// Whether the button of a column fetches one of the parts it covers - the plugin or its effect
	// build - given what that part needs (_part) and what the column offers for the two (_column). A
	// column that offers an install, upgrade or downgrade fetches the parts that need it and leaves the
	// current one alone: downloading a plugin that is up to date again, only because its effect build
	// was behind, was a reinstall nobody asked for (BUG-10486). A column that offers a reinstall, which
	// means everything in it is current, reinstalls all of it, because that is what its button says.
	bool fetchesPart(InstallAction _part, InstallAction _column);
}
