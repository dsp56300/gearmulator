#pragma once

#include <string>

namespace pluginManager
{
	// Where the catalog is read from. These are variables rather than constants so that a test can
	// point the library at files on disk; nothing else changes them at runtime.
	struct Urls
	{
		// Public builds are read straight from the GitHub releases API. One request returns every
		// release with its files, which is what makes the older versions offerable for a rollback.
		//
		// Second choice, though. GitHub allows sixty requests an hour from an address, which is one
		// number for a whole office, and this program asks on every start - so what is asked first
		// is our own copy of those same answers below.
		std::string githubReleases = "https://api.github.com/repos/dsp56300/gearmulator/releases?per_page=100";

		// That copy: a file listing the released versions, and one file per version holding what the
		// API answered for it. Nothing here keeps it up to date, so a release that has not been
		// copied over yet is one this program will not offer until GitHub is asked instead.
		std::string releaseCache = "https://theusualsuspects.io/builds/";

		std::string releaseIndex() const { return releaseCache + "versions.json"; }
		std::string cachedRelease(const std::string& _version) const { return releaseCache + _version + ".json"; }

		// The changelog of everything published, as it is in the public repository: every version in
		// it, also those that never had a release of their own. The notes GitHub keeps with a release
		// hold only some of them.
		std::string publicChangelog = "https://raw.githubusercontent.com/dsp56300/gearmulator/main/doc/changelog.txt";

		// Internal and donator builds are not on GitHub. They sit on our own server behind HTTP basic
		// auth, one folder per product and channel. The path keeps the /builds/ the old host had, so
		// that a folder link from either one names the folder the same way - see folderName.
		std::string serverBuilds = "https://builds.theusualsuspects.io/builds/";

		// Where they were before, which the old host answered with "too many requests" after a long
		// run of downloads. A file a product list still points at there gets the same login.
		std::string previousServerBuilds = "https://dsp56300.com/builds/";

		// Product display data. It holds names, descriptions and images only - no versions and no
		// download URLs - so it does not need touching when a release is published.
		// Still pointed at the old plugins.json, whose name and image fields are read as-is.
		std::string publicProducts = "https://theusualsuspects.io/installer/plugins.json";

		// The internal half of that list is behind the team login, because the names of unreleased
		// products are exactly what the public build index is configured to hide.
		std::string internalProducts() const { return serverBuilds + "internal/products.json"; }
	};

	Urls& urls();

	// What its own builds are called, which is the CMake component name and so the product in
	// their file names. Not what it calls itself: the file in the archive is "TUS Plugin Manager".
	static constexpr const char* g_appProduct = "PluginManager";

	// A product like any other to the catalog, which is how its own builds are found - and never a
	// row, nor anything a row is made of. Asked in three places, so it is asked the same way.
	inline bool isAppProduct(const std::string& _product) { return _product == g_appProduct; }

	// The name the program is known by to the system, and the one it calls itself in its own window.
	static constexpr const char* g_appName = "TUS Plugin Manager";
	static constexpr const char* g_appTitle = "The Usual Suspects - Plugin Manager";

	// Set from the project version, so it says what release it shipped with.
#ifndef TUS_PLUGIN_MANAGER_VERSION
#define TUS_PLUGIN_MANAGER_VERSION "0.0.0"
#endif
	static constexpr const char* g_appVersion = TUS_PLUGIN_MANAGER_VERSION;

	static constexpr const char* g_websiteUrl = "https://theusualsuspects.io";
}
