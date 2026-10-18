#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "pluginManagerLib/asset.h"
#include "pluginManagerLib/backup.h"
#include "pluginManagerLib/catalog.h"
#include "pluginManagerLib/catalogSource.h"
#include "pluginManagerLib/config.h"
#include "pluginManagerLib/credentials.h"
#include "pluginManagerLib/directoryIndex.h"
#include "pluginManagerLib/elevatedTask.h"
#include "pluginManagerLib/httpClient.h"
#include "pluginManagerLib/installPaths.h"
#include "pluginManagerLib/installedPlugins.h"
#include "pluginManagerLib/pluginInstaller.h"
#include "pluginManagerLib/pluginManager.h"
#include "pluginManagerLib/loginFile.h"
#include "pluginManagerLib/selfUpdate.h"
#include "pluginManagerLib/zipExtractor.h"

#include "changelogLib/changelog.h"

#include "juce_core/juce_core.h"

#ifndef _WIN32
#include <sys/stat.h>		// the file mode of the stored logins, which only these systems have
#endif

using namespace pluginManager;

// Works in Release too, where assert() is compiled out.
#define TEST_ASSERT(condition)                                                     \
	do {                                                                           \
		if (!(condition)) {                                                        \
			std::ostringstream oss;                                                \
			oss << "Test assertion failed: " << #condition                         \
			    << " at " << __FILE__ << ":" << __LINE__;                          \
			throw std::runtime_error(oss.str());                                   \
		}                                                                          \
	} while (false)

namespace
{
	Asset parse(const std::string& _name)
	{
		Asset a;
		TEST_ASSERT(parseAssetName(_name, a));
		return a;
	}

	// A zip holding everything in a folder, laid out the way the folder is. Three tests build one.
	// TUS_DATA_FOLDER is what both the plugin and this program use in place of the user's
	// documents folder when it is set, so it is also how a test gets its own one.
	void setDataFolder(const std::string& _path)
	{
#ifdef _WIN32
		_putenv_s("TUS_DATA_FOLDER", _path.c_str());
#else
		if (_path.empty())
			unsetenv("TUS_DATA_FOLDER");
		else
			setenv("TUS_DATA_FOLDER", _path.c_str(), 1);
#endif
	}

	void makeZip(const juce::File& _content, const juce::File& _zip)
	{
		juce::ZipFile::Builder builder;

		for (const auto& file : _content.findChildFiles(juce::File::findFiles, true))
			builder.addFile(file, 0, file.getRelativePathFrom(_content).replaceCharacter('\\', '/'));

		juce::FileOutputStream stream(_zip);
		TEST_ASSERT(stream.openedOk());
		TEST_ASSERT(builder.writeToStream(stream, nullptr));
	}

	bool rejects(const std::string& _name)
	{
		Asset a;
		return !parseAssetName(_name, a);
	}

	void testPluginAsset()
	{
		const auto a = parse("TheUsualSuspects-Osirus-VST3-2.2.16-win64.zip");

		TEST_ASSERT(a.product == "Osirus");
		TEST_ASSERT(a.format == "VST3");
		TEST_ASSERT(a.version == "2.2.16");
		TEST_ASSERT(a.os == "windows");
		TEST_ASSERT(a.arch == "x86_64");
		TEST_ASSERT(a.package == "zip");
	}

	void testPlatforms()
	{
		const auto mac = parse("TheUsualSuspects-JE8086-AU-2.2.16-MacOS.zip");
		TEST_ASSERT(mac.os == "macos" && mac.arch == "universal");

		const auto lx = parse("TheUsualSuspects-Osirus-CLAP-2.2.15-Linux_x86_64.deb");
		TEST_ASSERT(lx.os == "linux" && lx.arch == "x86_64" && lx.package == "deb");

		const auto arm = parse("TheUsualSuspects-Osirus-LV2-2.2.15-Linux_aarch64.rpm");
		TEST_ASSERT(arm.os == "linux" && arm.arch == "aarch64" && arm.package == "rpm");
	}

	// FX and the consoles are separate products, not a nested field on the base product.
	void testVariantsAreProducts()
	{
		TEST_ASSERT(parse("TheUsualSuspects-OsirusFX-VST3-2.2.16-win64.zip").product == "OsirusFX");
		TEST_ASSERT(parse("TheUsualSuspects-XeniaFX-CLAP-2.2.16-MacOS.zip").product == "XeniaFX");
	}

	// Several products have a dash in the name, so the name cannot be parsed left to right - doing
	// that yields product "Synth" with format "1000". The shapes match real build names.
	void testProductNamesWithDashes()
	{
		const auto dashed = parse("TheUsualSuspects-Synth-1000-AU-2.2.19-MacOS.zip");
		TEST_ASSERT(dashed.product == "Synth-1000");
		TEST_ASSERT(dashed.format == "AU");
		TEST_ASSERT(dashed.version == "2.2.19");

		const auto twoDashes = parse("TheUsualSuspects-Rack-A-100-CLAP-2.2.17-win64.zip");
		TEST_ASSERT(twoDashes.product == "Rack-A-100");
		TEST_ASSERT(twoDashes.format == "CLAP");

		// A dashed product with no format segment: still all of it the product, not a format called
		// "1000" - and a program, which is what no format segment means.
		const auto bare = parse("TheUsualSuspects-Synth-1000-2.2.19-win64.zip");
		TEST_ASSERT(bare.product == "Synth-1000");
		TEST_ASSERT(bare.format == "Standalone");
	}

	// Standalone ships in internal builds only, so it is missing from the website's format list.
	void testStandaloneFormat()
	{
		const auto a = parse("TheUsualSuspects-DemoPlayer-Standalone-2.2.19-Linux_aarch64.deb");
		TEST_ASSERT(a.product == "DemoPlayer");
		TEST_ASSERT(a.format == "Standalone");
		TEST_ASSERT(a.os == "linux" && a.arch == "aarch64");

		// ...whereas a product whose name merely ends in Standalone keeps all of it as its name.
		// It is in that format all the same, because it carries no format segment at all - but by
		// being a program, not by what it is called.
		const auto j = parse("TheUsualSuspects-DemoStandalone-2.2.14-Linux_x86_64.zip");
		TEST_ASSERT(j.product == "DemoStandalone");
		TEST_ASSERT(j.format == "Standalone");
	}

	// Three part names carry no format segment: the bridge server, test consoles, perf tests. They
	// are programs rather than plugins, which is what Standalone means, and reading them as that
	// is what gives them somewhere to install to - without it they could only ever be listed.
	void testFormatlessAsset()
	{
		const auto a = parse("TheUsualSuspects-DSPBridgeServer-2.2.16-Linux_x86_64.zip");

		TEST_ASSERT(a.product == "DSPBridgeServer");
		TEST_ASSERT(a.format == "Standalone");
		TEST_ASSERT(a.version == "2.2.16");

		TEST_ASSERT(parse("TheUsualSuspects-OsirusTestConsole-2.2.16-win64.zip").format == "Standalone");
		TEST_ASSERT(parse("TheUsualSuspects-VavraPerformanceTest-2.2.16-MacOS.zip").format == "Standalone");
	}

	// A directory listing carries more than our own assets, so anything unrecognised must be
	// dropped rather than turned into a half filled entry with a bogus download URL.
	void testRejectsForeignNames()
	{
		TEST_ASSERT(rejects("SomethingElse-Osirus-VST3-2.2.16-win64.zip"));
		TEST_ASSERT(rejects("TheUsualSuspects-Osirus-VST3-2.2.16-win64.txt"));
		TEST_ASSERT(rejects("TheUsualSuspects-Osirus-VST3-2.2.16-solaris.zip"));
		TEST_ASSERT(rejects("TheUsualSuspects-Osirus-win64.zip"));		// no version
		TEST_ASSERT(rejects("TheUsualSuspects-Osirus-VST3-notaversion-win64.zip"));
		TEST_ASSERT(rejects("TheUsualSuspects-"));
		TEST_ASSERT(rejects(""));
		TEST_ASSERT(rejects("index.html"));
	}

	// The reason this exists: as text, "2.2.9" > "2.2.16", so a version list sorted as strings
	// offers the wrong newest build.
	void testVersionOrdering()
	{
		TEST_ASSERT(compareVersions("2.2.9", "2.2.16") < 0);
		TEST_ASSERT(compareVersions("2.2.16", "2.2.9") > 0);
		TEST_ASSERT(compareVersions("2.2.16", "2.2.16") == 0);
		TEST_ASSERT(compareVersions("2.2", "2.2.0") == 0);
		TEST_ASSERT(compareVersions("1.4.4", "2.0.13") < 0);
		TEST_ASSERT(compareVersions("2.10.0", "2.9.0") > 0);
	}

	// What the button offers, which both the list and the detail page ask for.
	void testInstallActions()
	{
		TEST_ASSERT(decideInstallAction(false, {}, "2.2.25") == InstallAction::Install);
		TEST_ASSERT(decideInstallAction(true, "2.2.24", "2.2.25") == InstallAction::Upgrade);
		TEST_ASSERT(decideInstallAction(true, "2.2.25", "2.2.24") == InstallAction::Downgrade);
		TEST_ASSERT(decideInstallAction(true, "2.2.25", "2.2.25") == InstallAction::Reinstall);

		// A build that cannot say what it is gets a reinstall rather than a promise.
		TEST_ASSERT(decideInstallAction(true, {}, "2.2.25") == InstallAction::Reinstall);

		// Nothing is installed, whatever version is picked.
		TEST_ASSERT(decideInstallAction(false, "2.2.25", "2.2.24") == InstallAction::Install);
	}

	// One button for a plugin and the effect build beside it: the more pressing of the two, so
	// that a plugin which is up to date next to an effect build that is missing still offers to
	// fetch something.
	void testPairedInstallActions()
	{
		using IA = InstallAction;

		// Missing comes before behind, from either half.
		TEST_ASSERT(combineInstallActions(IA::Reinstall, IA::Install) == IA::Install);
		TEST_ASSERT(combineInstallActions(IA::Upgrade, IA::Install) == IA::Install);
		TEST_ASSERT(combineInstallActions(IA::Install, IA::Reinstall) == IA::Install);

		// Behind comes before nothing to do.
		TEST_ASSERT(combineInstallActions(IA::Reinstall, IA::Upgrade) == IA::Upgrade);
		TEST_ASSERT(combineInstallActions(IA::Upgrade, IA::Reinstall) == IA::Upgrade);

		// A downgrade is only reachable where an older version can be picked, and loses to both.
		TEST_ASSERT(combineInstallActions(IA::Downgrade, IA::Reinstall) == IA::Downgrade);
		TEST_ASSERT(combineInstallActions(IA::Downgrade, IA::Upgrade) == IA::Upgrade);

		// Nothing either of them needs.
		TEST_ASSERT(combineInstallActions(IA::Reinstall, IA::Reinstall) == IA::Reinstall);
	}

	// Which parts of a column its button fetches (BUG-10486).
	void testAColumnFetchesOnlyWhatNeedsIt()
	{
		using IA = InstallAction;

		// The plugin current, its effect build behind: the column offers the upgrade, and the upgrade
		// fetches the effect build only. It fetched the plugin again as well.
		TEST_ASSERT(!fetchesPart(IA::Reinstall, IA::Upgrade));
		TEST_ASSERT(fetchesPart(IA::Upgrade, IA::Upgrade));

		// The same for one that is missing, and for an older version that was picked.
		TEST_ASSERT(!fetchesPart(IA::Reinstall, IA::Install));
		TEST_ASSERT(fetchesPart(IA::Install, IA::Install));
		TEST_ASSERT(!fetchesPart(IA::Reinstall, IA::Downgrade));
		TEST_ASSERT(fetchesPart(IA::Downgrade, IA::Downgrade));

		// A part that needs something other than what the column offers is still fetched: the column
		// shows only the more pressing of the two.
		TEST_ASSERT(fetchesPart(IA::Downgrade, IA::Upgrade));

		// Everything current: the column offers a reinstall, and its button reinstalls all of it.
		TEST_ASSERT(fetchesPart(IA::Reinstall, IA::Reinstall));
	}

	// Whether a column's install or upgrade takes the effect build along (BUG-10488).
	void testEffectBuildIsIncludedWhenAskedForOrInstalled()
	{
		// Asked for with the FX chip, whether it is installed yet or not.
		TEST_ASSERT(includesFxBuild(true, true, false));
		TEST_ASSERT(includesFxBuild(true, true, true));

		// Not asked for but installed: kept in step with its plugin. Going by the chip alone, an
		// installed effect build was never upgraded while "Updates available" counted it.
		TEST_ASSERT(includesFxBuild(true, false, true));

		// Neither: an install of the plugin does not bring an effect build nobody asked for.
		TEST_ASSERT(!includesFxBuild(true, false, false));

		// No effect build of that version to fetch, whatever else is true.
		TEST_ASSERT(!includesFxBuild(false, true, true));
		TEST_ASSERT(!includesFxBuild(false, true, false));
		TEST_ASSERT(!includesFxBuild(false, false, true));
	}

	// A trimmed but faithful copy of what the server actually returns for a channel folder: it is
	// a generated listing, so the build links sit among a parent folder link, column sort links
	// and asset references.
	const char* g_indexHtml =
		"<link rel=\"stylesheet\" href=\"/_autoindex/assets/css/autoindex.css\" />"
		"<script src=\"/_autoindex/assets/js/tablesort.js\"></script>"
		"<th><a class=\"name\" href=\"?ND\" onclick=\"return false\">Name</a></th>"
		"<tr><td><a href=\"/builds/osirus/\">Parent Directory</a></td></tr>"
		"<tr><td data-sort=\"theusualsuspects-osirus-au-2.2.14-macos.zip\">"
		"<a href=\"/builds/osirus/internal/TheUsualSuspects-Osirus-AU-2.2.14-MacOS.zip\">"
		"TheUsualSuspects-Osirus-AU-2.2.14-MacOS.zip</a></td></tr>"
		"<tr><td><a href=\"/builds/osirus/internal/TheUsualSuspects-Osirus-VST3-2.2.15-win64.zip\">x</a></td></tr>"
		"<tr><td><a href=\"/builds/osirus/internal/readme.txt\">readme.txt</a></td></tr>";

	void testDirectoryIndex()
	{
		const std::string dir = "https://dsp56300.com/builds/osirus/internal/";
		const auto assets = parseDirectoryIndex(g_indexHtml, dir, Channel::Internal);

		// The two builds, and neither the navigation links nor readme.txt.
		TEST_ASSERT(assets.size() == 2);

		TEST_ASSERT(assets[0].product == "Osirus");
		TEST_ASSERT(assets[0].format == "AU");
		TEST_ASSERT(assets[0].version == "2.2.14");
		TEST_ASSERT(assets[0].channel == Channel::Internal);
		TEST_ASSERT(assets[0].url == dir + "TheUsualSuspects-Osirus-AU-2.2.14-MacOS.zip");

		TEST_ASSERT(assets[1].format == "VST3");
		TEST_ASSERT(assets[1].url == dir + "TheUsualSuspects-Osirus-VST3-2.2.15-win64.zip");
	}

	// The file name appears both in the link target and as the link text, so a naive scan reports
	// every build twice and the version list shows duplicates.
	void testDirectoryIndexDoesNotDuplicate()
	{
		const char* html =
			"<a href=\"/builds/synth-1000/internal/TheUsualSuspects-Synth-1000-CLAP-2.2.19-win64.zip\">"
			"TheUsualSuspects-Synth-1000-CLAP-2.2.19-win64.zip</a>"
			"<a href=\"/builds/synth-1000/internal/TheUsualSuspects-Synth-1000-CLAP-2.2.19-win64.zip\">again</a>";

		const auto assets = parseDirectoryIndex(html, "https://dsp56300.com/builds/synth-1000/internal/", Channel::Donators);

		TEST_ASSERT(assets.size() == 1);
		TEST_ASSERT(assets[0].product == "Synth-1000");
		TEST_ASSERT(assets[0].channel == Channel::Donators);
	}

	void testEmptyDirectoryIndex()
	{
		TEST_ASSERT(parseDirectoryIndex("", "https://x/", Channel::Internal).empty());
		TEST_ASSERT(parseDirectoryIndex("<html><body>nothing here</body></html>", "https://x/", Channel::Internal).empty());
	}
	Catalog buildCatalog()
	{
		const char* names[] = {
			"TheUsualSuspects-Osirus-VST3-2.2.9-win64.zip",
			"TheUsualSuspects-Osirus-VST3-2.2.16-win64.zip",
			"TheUsualSuspects-Osirus-CLAP-2.2.16-win64.zip",
			"TheUsualSuspects-Osirus-AU-2.2.16-MacOS.zip",
			"TheUsualSuspects-OsirusFX-VST3-2.2.16-win64.zip",
			"TheUsualSuspects-OsirusTestConsole-2.2.16-win64.zip",
			"TheUsualSuspects-Osirus-CLAP-2.2.16-Linux_x86_64.deb",
			"TheUsualSuspects-Osirus-CLAP-2.2.16-Linux_x86_64.zip",
		};

		std::vector<Asset> assets;

		for (const auto* n : names)
		{
			Asset a;
			TEST_ASSERT(parseAssetName(n, a));
			assets.push_back(a);
		}

		Catalog c;
		c.addAssets(assets);
		return c;
	}

	// The whole point of the version list: newest first, and ordered numerically so 2.2.16 leads
	// rather than 2.2.9.
	void testVersionList()
	{
		const auto c = buildCatalog();
		const auto v = c.getVersions("Osirus", "VST3", "windows", "x86_64");

		TEST_ASSERT(v.size() == 2);
		TEST_ASSERT(v[0] == "2.2.16");
		TEST_ASSERT(v[1] == "2.2.9");

		// A build for another platform must not show up as an installable version here.
		TEST_ASSERT(c.getVersions("Osirus", "AU", "windows", "x86_64").empty());
		TEST_ASSERT(c.getVersions("Osirus", "AU", "macos", "universal").size() == 1);
	}

	void testFormatList()
	{
		const auto c = buildCatalog();
		const auto f = c.getFormats("Osirus", "windows", "x86_64");

		TEST_ASSERT(f.size() == 2);
		TEST_ASSERT(f[0] == "VST3");		// display order, not arrival order
		TEST_ASSERT(f[1] == "CLAP");

		// The test console is a program, so it is offered as a Standalone - and on its own, because
		// it is a product of its own in the build names. An empty list here is what used to keep it
		// out of the list and out of reach of the command line, since nothing with no format to
		// install has anywhere to install to.
		const auto t = c.getFormats("OsirusTestConsole", "windows", "x86_64");

		TEST_ASSERT(t.size() == 1 && t[0] == "Standalone");

		// Which says nothing about the plugin: a variant is not one of its formats.
		TEST_ASSERT(std::find(f.begin(), f.end(), "Standalone") == f.end());
	}

	// A plugin has more beside it than its effect build: a test console is a variant of the same
	// name too. So whatever wants the effect build has to ask for it by name rather than take the
	// first of these - which would be right here only by the luck of the alphabet.
	// A login handed over as a file or a pasted line, so that somebody being given a build to try
	// is not given a folder name, a user and a password to type out of a chat message.
	void testALoginCanBeHandedOverInAFile()
	{
		std::vector<SharedLogin> logins{
			{"synth-1000", {"s1", "hunter2"}},
			{"rack-a-100", {"ra", "letmein"}}};

		const auto text = LoginFile::write(logins);

		// One line, because the same text is both a file to send and something to paste.
		TEST_ASSERT(!text.empty());
		TEST_ASSERT(text.find('\n') == std::string::npos);

		// And no password is in it to be read at a glance, which is the whole of what the
		// obfuscation buys - see LoginFile.
		TEST_ASSERT(text.find("hunter2") == std::string::npos);
		TEST_ASSERT(text.find("letmein") == std::string::npos);

		std::vector<SharedLogin> read;

		TEST_ASSERT(LoginFile::parse(text, read));
		TEST_ASSERT(read.size() == 2);
		TEST_ASSERT(read[0].slug == "synth-1000" && read[0].credentials.user == "s1");
		TEST_ASSERT(read[0].credentials.password == "hunter2");
		TEST_ASSERT(read[1].slug == "rack-a-100" && read[1].credentials.password == "letmein");

		// The folder is part of the key, so a blob cannot be lifted into a file claiming another
		// one: a login for somebody else's folder is not access to it.
		TEST_ASSERT(LoginFile::reveal(LoginFile::hide("hunter2", "synth-1000"), "rack-a-100") != "hunter2");

		// Anything else that lands on the window is not this, and has to be told apart from it
		// rather than read as a login carrying none.
		std::vector<SharedLogin> none;

		TEST_ASSERT(!LoginFile::parse("", none));
		TEST_ASSERT(!LoginFile::parse("not json at all", none));
		TEST_ASSERT(!LoginFile::parse(R"({"something":"else"})", none));
		TEST_ASSERT(!LoginFile::parse(R"({"tusLogin":1,"logins":[]})", none));

		// Half a login is no login: this would put a folder nothing can be fetched from into a
		// confirmation box.
		TEST_ASSERT(!LoginFile::parse(R"({"tusLogin":1,"logins":[{"slug":"x","user":"y"}]})", none));
		TEST_ASSERT(none.empty());

		// A version this does not know is refused rather than guessed at.
		TEST_ASSERT(!LoginFile::parse(R"({"tusLogin":99,"logins":[{"slug":"x","user":"y","pw":"z"}]})", none));
	}

	void testVariantsFound()
	{
		const auto c = buildCatalog();
		const auto v = c.getVariants("Osirus");

		TEST_ASSERT(v.size() == 2);
		TEST_ASSERT(v[0] == "OsirusFX");
		TEST_ASSERT(v[1] == "OsirusTestConsole");

		TEST_ASSERT(c.fxProductOf("Osirus") == "OsirusFX");

		// A variant has none of its own, and neither has a product that ships without one.
		TEST_ASSERT(c.fxProductOf("OsirusTestConsole").empty());
		TEST_ASSERT(c.fxProductOf("OsirusFX").empty());
	}

	// Linux ships the same build as zip, deb and rpm; the zip is the one that installs anywhere.
	void testPrefersZip()
	{
		const auto c = buildCatalog();
		const auto* a = c.find("Osirus", "CLAP", "2.2.16", "linux", "x86_64");

		TEST_ASSERT(a);
		TEST_ASSERT(a->package == "zip");
	}

	// A macOS universal build has to satisfy an arm machine and an intel one alike.
	void testUniversalMatchesAnyArch()
	{
		Asset a;
		TEST_ASSERT(parseAssetName("TheUsualSuspects-Osirus-AU-2.2.16-MacOS.zip", a));

		TEST_ASSERT(matchesPlatform(a, "macos", "aarch64"));
		TEST_ASSERT(matchesPlatform(a, "macos", "x86_64"));
		TEST_ASSERT(!matchesPlatform(a, "windows", "x86_64"));
	}

	// A release that also sits in a protected channel is still public, and the public copy is the one
	// downloaded, because it needs no login. A build offered to both donators and the team is labelled
	// by the wider of the two audiences.
	void testChannelPreference()
	{
		Asset published;
		TEST_ASSERT(parseAssetName("TheUsualSuspects-Osirus-VST3-2.2.16-win64.zip", published));
		published.url = "public";

		auto protectedCopy = published;
		protectedCopy.channel = Channel::Internal;
		protectedCopy.url = "internal";

		Asset teamBuild;
		TEST_ASSERT(parseAssetName("TheUsualSuspects-Osirus-VST3-2.2.19-win64.zip", teamBuild));
		teamBuild.channel = Channel::Internal;

		auto donatorBuild = teamBuild;
		donatorBuild.channel = Channel::Donators;

		Catalog c;
		// The protected copy first, so a first-match lookup would pick the wrong one.
		c.addAssets({protectedCopy, published, teamBuild, donatorBuild});

		TEST_ASSERT(c.getChannel("Osirus", "VST3", "2.2.16", "windows", "x86_64") == Channel::Public);
		TEST_ASSERT(c.find("Osirus", "VST3", "2.2.16", "windows", "x86_64")->url == "public");
		TEST_ASSERT(c.getChannel("Osirus", "VST3", "2.2.19", "windows", "x86_64") == Channel::Donators);
		TEST_ASSERT(c.find("Osirus", "VST3", "2.2.19", "windows", "x86_64")->channel == Channel::Donators);
	}

	// The internal list fills in a category the public one left out, and does not overwrite one it gave.
	void testAddProductsKeepsTheFirstCategory()
	{
		Product publicOne;
		publicOne.product = "Synth-1000";

		Product internalOne = publicOne;
		internalOne.category = "Rompler";

		Catalog c;
		c.setProducts({publicOne});
		c.addProducts({internalOne});
		TEST_ASSERT(c.getProducts()[0].category == "Rompler");

		internalOne.category = "Effect";
		c.addProducts({internalOne});
		TEST_ASSERT(c.getProducts()[0].category == "Rompler");
	}

	// By manufacturer from A to Z whatever the case, then oldest first, and what is missing goes last.
	void testProductsSortByManufacturerThenYear()
	{
		const auto product = [](const std::string& _manufacturer, const int _year)
		{
			Product p;
			p.manufacturer = _manufacturer;
			p.year = _year;
			return p;
		};

		TEST_ASSERT(sortsBefore(product("Acme", 2001), product("Brand", 1990)));
		TEST_ASSERT(sortsBefore(product("acme", 2001), product("Brand", 1990)));
		TEST_ASSERT(sortsBefore(product("Brand", 1990), product("Brand", 1995)));
		TEST_ASSERT(sortsBefore(product("Brand", 1995), product("Brand", 0)));
		TEST_ASSERT(sortsBefore(product("Zeta", 0), product("", 1980)));

		// Equal, either way round, so that a stable sort keeps the list order.
		TEST_ASSERT(!sortsBefore(product("Brand", 1990), product("brand", 1990)));
		TEST_ASSERT(!sortsBefore(product("brand", 1990), product("Brand", 1990)));
		TEST_ASSERT(!sortsBefore(product("", 0), product("", 0)));
	}

	// The internal product list can repeat a public product; it must not turn into a second row.
	void testAddProductsSkipsKnown()
	{
		Product known;
		known.product = "Osirus";

		Product extra;
		extra.product = "Synth-1000";

		// A later list names files for a product the first one has: they are added to it, nothing else is.
		auto repeated = known;
		repeated.name = "Not this name";
		repeated.files = {{"extras", {{"https://example.com/file.bin", {}}}}};

		Catalog c;
		c.setProducts({known});
		c.addProducts({repeated, extra});

		TEST_ASSERT(c.getProducts().size() == 2);
		TEST_ASSERT(c.getProducts()[1].product == "Synth-1000");
		TEST_ASSERT(c.getProducts()[0].name.empty());
		TEST_ASSERT(c.getProducts()[0].files.size() == 1);
		TEST_ASSERT(c.getProducts()[0].files[0].folder == "extras");
	}

	// Everything below drives the library the way a program does - PluginManager against a catalog on
	// disk - rather than one piece at a time. The names are synthetic; what they have to be faithful
	// about is their shape.
	//
	// A folder answers the way the server does, with the index page inside it, so the code path that
	// reads a channel from the server is the one that reads a channel from here.
	struct Fixture
	{
		juce::File dir;
		Urls saved = urls();

		Fixture()
		{
			dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
				.getChildFile("pluginManagerTest-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

			write("products.json", R"({"products":[
				{"product":"Synth-1000","slug":"synth-1000","name":"Synth 1000","description":"A synth","category":"Virtual Analog",
				 "manufacturer":"Acme","originalName":"S-1000","year":1997},
				{"product":"Rack-A-100","slug":"rack-a-100","name":"Rack A 100"}]})");

			write("releases.json", R"([{"assets":[
				{"name":"TheUsualSuspects-Synth-1000-VST3-2.2.16-win64.zip","browser_download_url":"https://releases/s1000-vst3.zip"},
				{"name":"TheUsualSuspects-Rack-A-100-CLAP-2.2.16-win64.zip","browser_download_url":"https://releases/ra100-clap.zip"},
				{"name":"not-one-of-ours.zip","browser_download_url":"https://releases/other.zip"}]}])");

			// Our copy of the same answer, which is what the manager reads first: a list of released
			// versions and one file per version. Same builds as releases.json, so that every test
			// below sees the catalog it always saw whichever source it came from.
			write("cache/versions.json", R"(["2.2.16"])");

			write("cache/2.2.16.json", R"({"assets":[
				{"name":"TheUsualSuspects-Synth-1000-VST3-2.2.16-win64.zip","browser_download_url":"https://releases/s1000-vst3.zip"},
				{"name":"TheUsualSuspects-Rack-A-100-CLAP-2.2.16-win64.zip","browser_download_url":"https://releases/ra100-clap.zip"},
				{"name":"not-one-of-ours.zip","browser_download_url":"https://releases/other.zip"}]})");

			// The console is a product of its own here, exactly as the real list has them - being
			// listed is what gives it a row. Without it in here, a test asking whether something is a
			// variant gets the right answer for the wrong reason and says nothing about what happens.
			write("builds/internal/products.json", R"({"products":[
				{"product":"Synth-1000","slug":"synth-1000","name":"Synth 1000"},
				{"product":"Synth-1000TestConsole","slug":"","name":"Synth 1000 Test Console"},
				{"product":"Prototype-9","slug":"prototype-9","name":"Prototype 9","category":"Rompler","year":"1991"}]})");

			write("builds/synth-1000/internal/index.html",
				R"(<a href="/builds/synth-1000/internal/TheUsualSuspects-Synth-1000-VST3-2.2.19-win64.zip">x</a>)"
				R"(<a href="/builds/synth-1000/internal/TheUsualSuspects-Synth-1000FX-VST3-2.2.19-win64.zip">x</a>)");

			write("builds/prototype-9/internal/index.html",
				R"(<a href="/builds/prototype-9/internal/TheUsualSuspects-Prototype-9-VST3-2.2.19-win64.zip">x</a>)");

			urls().publicProducts = url("products.json");
			urls().githubReleases = url("releases.json");
			urls().releaseCache = url("cache/");
			urls().serverBuilds = url("builds/");
		}

		~Fixture()
		{
			urls() = saved;
			dir.deleteRecursively();
		}

		void write(const std::string& _path, const std::string& _content) const
		{
			auto file = dir.getChildFile(juce::String(_path));
			file.getParentDirectory().createDirectory();
			TEST_ASSERT(file.replaceWithText(juce::String(_content)));
		}

		std::string url(const std::string& _path) const
		{
			auto path = dir.getChildFile(juce::String(_path)).getFullPathName().toStdString();

			// A folder keeps its trailing separator, which is what the rest of the library appends
			// a file name to. getChildFile drops it.
			if (!_path.empty() && _path.back() == '/')
				path += '/';

			return "file:///" + path;
		}
	};

	// The team login is handed over rather than stored, so the test neither reads nor writes the
	// logins of the machine it runs on.
	PluginManager makeManager(const bool _withTeamLogin)
	{
		PluginManager pm(false);

		if (_withTeamLogin)
			pm.setCredentials(Channel::Internal, {}, {"team", "secret"});

		return pm;
	}

	// A program listed for a row of its own ships from the folder of the product it belongs to. The list
	// says so with an empty slug, and such a product has no folder to ask for - asking cost a 404 per
	// program on every refresh. A product whose slug is not spelled out still has the default one.
	void testAnEmptySlugMeansNoFolderOfItsOwn()
	{
		const Fixture fixture;

		auto pm = makeManager(true);
		std::string error;
		TEST_ASSERT(pm.refresh(error));

		const auto* console = pm.getCatalog().findProduct("Synth-1000TestConsole");
		TEST_ASSERT(console && console->slug.empty());

		const auto* synth = pm.getCatalog().findProduct("Synth-1000");
		TEST_ASSERT(synth && synth->slug == "synth-1000");

		const auto* rack = pm.getCatalog().findProduct("Rack-A-100");
		TEST_ASSERT(rack && rack->slug == "rack-a-100");
	}

	// The category comes in with the rest of a product's display data, from either list, as it is
	// written there. A product the lists give none has none, whatever its name looks like.
	void testACategoryIsReadFromTheProductLists()
	{
		const Fixture fixture;

		auto pm = makeManager(true);
		std::string error;
		TEST_ASSERT(pm.refresh(error));

		const auto& catalog = pm.getCatalog();

		TEST_ASSERT(catalog.findProduct("Synth-1000")->category == "Virtual Analog");
		TEST_ASSERT(catalog.findProduct("Prototype-9")->category == "Rompler");
		TEST_ASSERT(catalog.findProduct("Synth-1000TestConsole")->category.empty());
		TEST_ASSERT(catalog.findProduct("Rack-A-100")->category.empty());

		// The hardware's maker, name and year the same way; a year also reads when it is written as a string.
		const auto* synth = catalog.findProduct("Synth-1000");
		TEST_ASSERT(synth->manufacturer == "Acme" && synth->originalName == "S-1000" && synth->year == 1997);

		const auto* prototype = catalog.findProduct("Prototype-9");
		TEST_ASSERT(prototype->manufacturer.empty() && prototype->originalName.empty() && prototype->year == 1991);

		const auto* rack = catalog.findProduct("Rack-A-100");
		TEST_ASSERT(rack->manufacturer.empty() && rack->originalName.empty() && rack->year == 0);
	}

	// EMU-153: the changelog as the Plugin Manager shows it, read by the code changelogGenerator splits it
	// with. Both shapes it arrives in: the file as it is written, and the release notes made from it.
	void testAChangelogIsReadPerProductAndVersion()
	{
		changelogLib::Changelog changelog;

		// No newline at the end, as a release body comes: its last line still counts.
		TEST_ASSERT(changelog.parse(
			"Release Notes\n"
			"\n"
			"2.2.26:\n"
			"\n"
			"Framework:\n"
			"\n"
			"- [Fix] Shared fix\n"
			"        that goes on\n"
			"        Version 2.2.8 broke it\n"
			"\n"
			"Synth-1000/Rack-A-100:\n"
			"\n"
			"- [Imp] For both\n"
			"\n"
			"DSP Bridge:\n"
			"\n"
			"- [Fix] Bridge fix\n"
			"\n"
			"Version 2.2.9\n"
			"\n"
			"Synth-1000:\n"
			"\n"
			"- [Fix] Old fix\n"
			"\n"
			"2.2.10:\n"
			"\n"
			"Framework:\n"
			"\n"
			"- [Fix] Only shared"));

		TEST_ASSERT(changelog.getProducts().size() == 3);

		// Written the way the catalog writes it: the build is "DSPBridgeServer", the heading "DSP Bridge".
		TEST_ASSERT(changelog.findProduct("synth-1000") == "Synth-1000");
		TEST_ASSERT(changelog.findProduct("DSPBridgeServer") == "DSP Bridge");
		TEST_ASSERT(changelog.findProduct("Prototype-9").empty());

		// Newest first by number - 2.2.10 is newer than 2.2.9, which sorting the text gets wrong - and
		// a sentence that starts with "Version" is not a version.
		const auto releases = changelog.getReleases("Synth-1000");

		TEST_ASSERT(releases.size() == 3);
		TEST_ASSERT(releases[0].version == "2.2.26");
		TEST_ASSERT(releases[1].version == "2.2.10");
		TEST_ASSERT(releases[2].version == "2.2.9");

		// What is about every product apart from what is about this one.
		TEST_ASSERT(releases[0].global.size() == 1);
		TEST_ASSERT(releases[0].global.at("Framework").size() == 3);
		TEST_ASSERT(releases[0].global.at("Framework")[1] == "        that goes on");
		TEST_ASSERT(releases[0].product == changelogLib::Lines{"- [Imp] For both"});

		TEST_ASSERT(releases[1].product.empty());
		TEST_ASSERT(releases[1].global.at("Framework") == changelogLib::Lines{"- [Fix] Only shared"});

		TEST_ASSERT(releases[2].global.empty());
		TEST_ASSERT(releases[2].product == changelogLib::Lines{"- [Fix] Old fix"});

		// A version with nothing for a product and nothing shared is not one of its versions.
		TEST_ASSERT(changelog.getReleases("Rack-A-100").size() == 2);

		// Copied out of the viewer, as text and as Markdown: a heading per section and an entry to a
		// line, not wrapped where the file wraps it.
		TEST_ASSERT(changelogLib::Changelog::toText(releases[0], "Synth-1000") ==
			"Synth-1000 Version 2.2.26\n"
			"\nFramework:\n\n- [Fix] Shared fix that goes on Version 2.2.8 broke it\n"
			"\nSynth-1000:\n\n- [Imp] For both\n");

		TEST_ASSERT(changelogLib::Changelog::toMarkdown(releases[0], "Synth-1000") ==
			"## Synth-1000 2.2.26\n"
			"\n### Framework\n\n- **Fix** Shared fix that goes on Version 2.2.8 broke it\n"
			"\n### Synth-1000\n\n- **Imp** For both\n");
	}

	// EMU-153: where the Plugin Manager reads a changelog from. A protected folder a login opens has the
	// product's own, put there by the build job; a published product falls back to the public one; a
	// product that is neither has none. The public one ends at the newest release, except for the team.
	void testAChangelogComesFromTheNewestPlaceThereIsOne()
	{
		const Fixture fixture;

		// The catalog's newest release is 2.2.16; main already says what 2.2.17 brings.
		fixture.write("public-changelog.txt",
			"2.2.17:\n\nFramework:\n\n- [Fix] Coming fix\n\n"
			"2.2.16:\n\nFramework:\n\n- [Fix] Published fix\n");
		urls().publicChangelog = fixture.url("public-changelog.txt");

		std::string error;

		const auto versionsOf = [&](const PluginManager& _pm, const std::string& _product)
		{
			changelogLib::Changelog changelog;
			std::vector<std::string> versions;

			if (!_pm.fetchChangelog(_product, changelog, error))
				return versions;

			for (const auto& release : changelog.getReleases(changelog.findProduct(_product)))
				versions.push_back(release.version);

			return versions;
		};

		// Without a login: the public one, up to what is out.
		{
			auto pm = makeManager(false);
			TEST_ASSERT(pm.refresh(error));

			TEST_ASSERT(versionsOf(pm, "Synth-1000") == std::vector<std::string>{"2.2.16"});
		}

		fixture.write("builds/synth-1000/internal/changelog.txt", "Version 2.2.19\n\nSynth-1000:\n\n- [Fix] Team fix\n");

		// With the team login: the internal folder, which is newer - for the effect build as well.
		{
			auto pm = makeManager(true);
			TEST_ASSERT(pm.refresh(error));

			TEST_ASSERT(versionsOf(pm, "Synth-1000") == std::vector<std::string>{"2.2.19"});
			TEST_ASSERT(versionsOf(pm, "Synth-1000FX") == std::vector<std::string>{"2.2.19"});

			// No internal folder for this one, so the public changelog - all of it: the team sees
			// what is coming.
			TEST_ASSERT((versionsOf(pm, "Rack-A-100") == std::vector<std::string>{"2.2.17", "2.2.16"}));

			// Never published and nothing in its folder: there is none, and the public changelog is
			// not passed off as its own.
			TEST_ASSERT(versionsOf(pm, "Prototype-9").empty());
			TEST_ASSERT(error.find("Prototype 9") != std::string::npos);
		}

		// A build can go straight to donators. With both logins the newer of the two changelogs is
		// shown: the internal one won because it was asked first, and a version only in donators never
		// showed up for the team.
		fixture.write("builds/synth-1000/donators/index.html", "");
		fixture.write("builds/synth-1000/donators/changelog.txt",
			"Version 2.2.20\n\nSynth-1000:\n\n- [Fix] Donator fix\n\nVersion 2.2.19\n\nSynth-1000:\n\n- [Fix] Team fix\n");

		const auto withBothLogins = [&]()
		{
			auto pm = makeManager(true);
			pm.setCredentials(Channel::Donators, "synth-1000", {"donator", "secret"});
			TEST_ASSERT(pm.refresh(error));
			return versionsOf(pm, "Synth-1000");
		};

		TEST_ASSERT((withBothLogins() == std::vector<std::string>{"2.2.20", "2.2.19"}));

		// And an older one in donators does not hide the newer internal one.
		fixture.write("builds/synth-1000/donators/changelog.txt", "Version 2.2.18\n\nSynth-1000:\n\n- [Fix] Old fix\n");

		TEST_ASSERT(withBothLogins() == std::vector<std::string>{"2.2.19"});
	}

	// Files a product brings besides its builds, listed section by section in its products.json and put
	// into the folders of its data folder those name. A build from a protected folder goes by that
	// folder's products.json; a file already there is only fetched again when the server has a newer
	// one; and a folder that points out of the data folder is refused.
	void testAdditionalFilesGoIntoTheDataFolder()
	{
		const Fixture fixture;

		const auto data = fixture.dir.getChildFile("documents");
		setDataFolder(data.getFullPathName().toStdString());

		fixture.write("files/Factory.mid", "factory");
		fixture.write("files/Night.skin", "skin");
		fixture.write("files/Team.syx", "team");

		const auto url = [&](const std::string& _path)
		{
			auto u = fixture.url(_path);
			std::replace(u.begin(), u.end(), '\\', '/');
			return u;
		};

		fixture.write("products.json", R"({"products":[
			{"product":"Synth-1000","slug":"synth-1000","name":"Synth 1000","files":[
				{"folder":"presets","urls":[")" + url("files/Factory.mid") + R"("]},
				{"folder":"skins/extra","urls":[")" + url("files/Night.skin") + R"(",
					{"url":")" + url("files/Night.skin") + R"(","path":"foo/bar/Renamed.skin"},
					{"url":")" + url("files/Night.skin") + R"(","path":"deep/"},
					{"url":")" + url("files/Night.skin") + R"(","path":"../../escape.skin"}]},
				{"folder":"../outside","urls":[")" + url("files/Factory.mid") + R"("]}]},
			{"product":"Rack-A-100","slug":"rack-a-100","name":"Rack A 100"}]})");

		const auto product = data.getChildFile("The Usual Suspects/Synth-1000");

		std::string error;
		std::string message;

		{
			auto pm = makeManager(false);
			TEST_ASSERT(pm.refresh(error));

			Asset build;
			TEST_ASSERT(parseAssetName("TheUsualSuspects-Synth-1000-VST3-2.2.16-win64.zip", build));

			// What arrives goes where the section and the file's own path say; what would land outside
			// - a section folder or a file path with ".." in it - is refused and said so.
			TEST_ASSERT(!pm.installAdditionalFiles(build, message));
			TEST_ASSERT(message.find("2 additional files could not be installed") != std::string::npos);
			TEST_ASSERT(product.getChildFile("presets/Factory.mid").loadFileAsString() == "factory");
			TEST_ASSERT(product.getChildFile("skins/extra/Night.skin").loadFileAsString() == "skin");
			TEST_ASSERT(product.getChildFile("skins/extra/foo/bar/Renamed.skin").loadFileAsString() == "skin");
			TEST_ASSERT(product.getChildFile("skins/extra/deep/Night.skin").loadFileAsString() == "skin");
			TEST_ASSERT(!product.getChildFile("escape.skin").exists());
			TEST_ASSERT(!data.getChildFile("The Usual Suspects/outside").exists());
			TEST_ASSERT(!product.getChildFile("presets/Factory.mid.tus-download").exists());

			// What the user changed is kept while the server has nothing newer...
			TEST_ASSERT(product.getChildFile("presets/Factory.mid").replaceWithText("mine"));
			pm.installAdditionalFiles(build, message);
			TEST_ASSERT(product.getChildFile("presets/Factory.mid").loadFileAsString() == "mine");

			// ...and replaced once it has.
			TEST_ASSERT(fixture.dir.getChildFile("files/Factory.mid")
				.setLastModificationTime(juce::Time::getCurrentTime() + juce::RelativeTime::hours(1)));
			pm.installAdditionalFiles(build, message);
			TEST_ASSERT(product.getChildFile("presets/Factory.mid").loadFileAsString() == "factory");

			// A product that lists none brings none, and that is no failure.
			Asset other;
			TEST_ASSERT(parseAssetName("TheUsualSuspects-Rack-A-100-CLAP-2.2.16-win64.zip", other));
			TEST_ASSERT(pm.installAdditionalFiles(other, message));
			TEST_ASSERT(message.empty());
		}

		// A build from the internal folder goes by that folder's own products.json. It is read with the
		// folder's listing, once per refresh rather than once per install, so the listing shows it.
		fixture.write("builds/synth-1000/internal/products.json", R"({"products":[{"product":"Synth-1000","files":[
			{"folder":"presets","urls":[")" + url("files/Team.syx") + R"("]}]}]})");

		fixture.write("builds/synth-1000/internal/index.html",
			R"(<a href="/builds/synth-1000/internal/TheUsualSuspects-Synth-1000-VST3-2.2.19-win64.zip">x</a>)"
			R"(<a href="/builds/synth-1000/internal/products.json">products.json</a>)");

		{
			auto pm = makeManager(true);
			TEST_ASSERT(pm.refresh(error));

			Asset build;
			TEST_ASSERT(parseAssetName("TheUsualSuspects-Synth-1000-VST3-2.2.19-win64.zip", build));
			build.channel = Channel::Internal;

			TEST_ASSERT(pm.installAdditionalFiles(build, message));
			TEST_ASSERT(message == "1 additional file installed");
			TEST_ASSERT(product.getChildFile("presets/Team.syx").loadFileAsString() == "team");
		}

		setDataFolder({});
	}

	// A product with anything installed that is behind the newest build on offer has an update, which is
	// what puts it into the "Updates available" group at the top of the list (EMU-250).
	void testAProductWithAnOlderInstallHasAnUpdate()
	{
		const Fixture fixture;

		const auto os = PlatformUtils::getCurrentOS();
		const auto platform = os == "windows" ? std::string("win64")
			: os == "macos" ? std::string("MacOS") : "Linux_" + PlatformUtils::getCurrentArch();

		fixture.write("cache/2.2.16.json", R"({"assets":[
			{"name":"TheUsualSuspects-Synth-1000-CLAP-2.2.16-)" + platform + R"(.zip","browser_download_url":"https://releases/s1000-clap.zip"},
			{"name":"TheUsualSuspects-Rack-A-100-CLAP-2.2.16-)" + platform + R"(.zip","browser_download_url":"https://releases/ra100-clap.zip"}]})");

		const auto folder = fixture.dir.getChildFile("clap");
		TEST_ASSERT(folder.createDirectory().wasOk());
		TEST_ASSERT(folder.getChildFile("Synth-1000.clap").replaceWithText("installed"));

		auto pm = makeManager(false);
		pm.getInstallPaths().set(PluginFormat::CLAP, folder.getFullPathName().toStdString());

		std::string error;
		pm.refresh(error);

		Asset installed;
		TEST_ASSERT(parseAssetName("TheUsualSuspects-Synth-1000-CLAP-2.2.15-" + platform + ".zip", installed));

		pm.recordInstall(installed, {"Synth-1000.clap"});
		TEST_ASSERT(pm.hasUpdate("Synth-1000"));

		// The newest is what is installed: nothing to update.
		installed.version = "2.2.16";
		pm.recordInstall(installed, {"Synth-1000.clap"});
		TEST_ASSERT(!pm.hasUpdate("Synth-1000"));

		// And nothing installed is no update either, whatever is on offer.
		TEST_ASSERT(!pm.hasUpdate("Rack-A-100"));
	}

	// A copy that cannot say which version it is - unpacked by hand, of a format whose file carries none -
	// used to be offered only a reinstall, whatever it was. Its file's date decides now: clearly older than
	// the build on offer is an upgrade, from about the same time is not.
	void testACopyWithoutAVersionIsJudgedByItsDate()
	{
		const Fixture fixture;

		const auto os = PlatformUtils::getCurrentOS();
		const auto platform = os == "windows" ? std::string("win64")
			: os == "macos" ? std::string("MacOS") : "Linux_" + PlatformUtils::getCurrentArch();

		// One release, published on the 20th of September.
		fixture.write("cache/2.2.16.json", R"({"published_at":"2026-09-20T20:08:24Z","assets":[
			{"name":"TheUsualSuspects-Synth-1000-CLAP-2.2.16-)" + platform + R"(.zip","browser_download_url":"https://releases/s1000-clap.zip"}]})");

		const auto folder = fixture.dir.getChildFile("clap");
		TEST_ASSERT(folder.createDirectory().wasOk());

		const auto file = folder.getChildFile("Synth-1000.clap");
		TEST_ASSERT(file.replaceWithText("carries no version"));

		auto pm = makeManager(false);
		pm.getInstallPaths().set(PluginFormat::CLAP, folder.getFullPathName().toStdString());

		std::string error;
		pm.refresh(error);

		TEST_ASSERT(pm.isInstalled("Synth-1000", "CLAP"));
		TEST_ASSERT(pm.getInstalledVersion("Synth-1000", "CLAP").empty());

		// Unpacked weeks before that release: behind it.
		TEST_ASSERT(file.setLastModificationTime(juce::Time(2026, 7, 1, 12, 0, 0, 0, true)));
		TEST_ASSERT(pm.isOutdated("Synth-1000", "CLAP", {}, "2.2.16"));
		TEST_ASSERT(pm.installAction("Synth-1000", "CLAP", true, {}, "2.2.16") == InstallAction::Upgrade);

		// Unpacked from it, a couple of hours off either way: the same build.
		TEST_ASSERT(file.setLastModificationTime(juce::Time(2026, 8, 20, 18, 0, 0, 0, true)));
		TEST_ASSERT(!pm.isOutdated("Synth-1000", "CLAP", {}, "2.2.16"));
		TEST_ASSERT(pm.installAction("Synth-1000", "CLAP", true, {}, "2.2.16") == InstallAction::Reinstall);

		// A copy that does say its version goes by the version, whatever the date of its file.
		TEST_ASSERT(file.setLastModificationTime(juce::Time(2026, 7, 1, 12, 0, 0, 0, true)));
		TEST_ASSERT(!pm.isOutdated("Synth-1000", "CLAP", "2.2.16", "2.2.16"));
		TEST_ASSERT(pm.isOutdated("Synth-1000", "CLAP", "2.2.15", "2.2.16"));
	}

	// The first install of this program is always one by hand, and only an update writes down which build
	// it is. A copy without that record is held against the date of its own file instead: a build of the
	// same version listed well after it is an update, one listed before it is not, and a record, once
	// there, is what decides.
	void testAHandInstalledCopyIsOfferedALaterBuildOfItsVersion()
	{
		const Fixture fixture;

		Asset asset;
		asset.product = g_appProduct;
		asset.format = "Standalone";
		asset.version = g_appVersion;
		asset.channel = Channel::Internal;

		auto pm = makeManager(true);

		TEST_ASSERT(pm.getInstallPaths().getSelfListed().empty());

		// This test's own binary was linked moments ago.
		asset.listed = "2001-01-01 00:00";
		TEST_ASSERT(!pm.isNewerBuild(asset));

		asset.listed = "2999-01-01 00:00";
		TEST_ASSERT(pm.isNewerBuild(asset));

		// A public release says what it is in its version number.
		asset.channel = Channel::Public;
		TEST_ASSERT(!pm.isNewerBuild(asset));
		asset.channel = Channel::Internal;

		pm.getInstallPaths().setSelfBuild("2999-06-01 00:00", 0);
		TEST_ASSERT(!pm.isNewerBuild(asset));

		asset.listed = "2999-07-01 00:00";
		TEST_ASSERT(pm.isNewerBuild(asset));
	}

	// Reported from a real machine: the status line said "Team login: Login rejected" although the
	// team login had been turned off in the settings file. The setting reached the window and
	// nothing else, so the login it had been told not to offer was still sent on every refresh.
	void testATeamLoginThatIsTurnedOffIsNotSent()
	{
		const Fixture fixture;

		PluginManager pm(false);

		// What a machine looks like after somebody logs in and turns the setting off again: the
		// login is still in the store, and nothing is meant to use it.
		pm.setCredentials(Channel::Internal, {}, {"team", "secret"});
		pm.getInstallPaths().setTeamLogin(false);

		std::string error;
		TEST_ASSERT(pm.refresh(error));

		// Nothing to say about a login nobody asked for.
		TEST_ASSERT(error.find("Team login") == std::string::npos);

		// And the products it would have brought are not there, which is what "off" means.
		TEST_ASSERT(pm.getCatalog().getProducts().size() == 2);
		TEST_ASSERT(!pm.getCatalog().findProduct("Prototype-9"));

		// Turned back on, the same stored login works again - off is not a logout.
		pm.getInstallPaths().setTeamLogin(true);
		TEST_ASSERT(pm.refresh(error));
		TEST_ASSERT(pm.getCatalog().findProduct("Prototype-9"));
	}

	// BUG-10461: installing six plugins into a folder that belongs to the system asked for
	// administrator rights six times. The copies are collected now and made together, so the shape
	// this tests is: an install hands its copy over instead of making it, and nothing is written
	// down until it has actually been made.
	void testElevatedCopiesAreCollectedAndRecordedOnlyWhenMade()
	{
		ElevatedBatch batch;

		TEST_ASSERT(batch.empty());

		Asset first;
		first.product = "Synth-1000";
		first.format = "VST3";
		first.version = "2.2.19";

		auto second = first;
		second.format = "CLAP";

		batch.add({"/staging/one", "/system/VST3"}, first, {"Synth-1000.vst3"});
		batch.add({"/staging/two", "/system/CLAP"}, second, {"Synth-1000.clap"});

		// Two copies, one prompt: the list is what the one request is made with.
		TEST_ASSERT(batch.size() == 2);
		TEST_ASSERT(batch.getJobs().size() == 2);
		TEST_ASSERT(batch.getJobs()[1].targetDir == "/system/CLAP");

		// The record travels with the copy rather than being written when the install finished, which
		// is the part that would otherwise claim a version that is not on disk.
		TEST_ASSERT(batch.getRecords().size() == 2);
		TEST_ASSERT(batch.getRecords()[0].asset.format == "VST3");
		TEST_ASSERT(batch.getRecords()[1].files.size() == 1);

		batch.clear();
		TEST_ASSERT(batch.empty() && batch.getRecords().empty());

		// Nothing waiting is not a failure, and must not put a prompt on screen for no reason.
		const Fixture fixture;
		auto pm = makeManager(false);

		std::string error;
		TEST_ASSERT(pm.finishPending(batch, error));
		TEST_ASSERT(error.empty());
	}

	// A folder this user owns is still copied into there and then. Collecting those as well would
	// make an ordinary install wait for the end of a run for no reason, and there is no prompt to
	// save in the first place.
	void testOnlyACopyThatNeedsRightsIsCollected()
	{
		const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("pluginManagerTest-pending");

		root.deleteRecursively();
		TEST_ASSERT(root.createDirectory().wasOk());

		const auto build = root.getChildFile("build");
		TEST_ASSERT(build.createDirectory().wasOk());
		TEST_ASSERT(build.getChildFile("Thing.vst3").replaceWithText("v1"));

		const auto zip = root.getChildFile("build.zip");
		makeZip(build, zip);

		const auto target = root.getChildFile("plugins");
		TEST_ASSERT(target.createDirectory().wasOk());

		std::string error;
		std::vector<std::string> files;

		ZipExtractor::PendingCopy pending;

		// No rights needed, so the list is offered and left alone: the file is simply there.
		TEST_ASSERT(ZipExtractor::install(zip.getFullPathName().toStdString(), target.getFullPathName().toStdString(),
			"Thing.vst3", "Thing", false, files, error, &pending));

		TEST_ASSERT(pending.job.targetDir.empty());
		TEST_ASSERT(target.getChildFile("Thing.vst3").existsAsFile());

		// Rights needed and a list given: unpacked, nothing copied, and the copy is what is waiting.
		const auto other = root.getChildFile("elevated");
		TEST_ASSERT(other.createDirectory().wasOk());

		TEST_ASSERT(ZipExtractor::install(zip.getFullPathName().toStdString(), other.getFullPathName().toStdString(),
			"Thing.vst3", "Thing", true, files, error, &pending));

		TEST_ASSERT(pending.job.targetDir == other.getFullPathName().toStdString());
		TEST_ASSERT(!pending.job.sourceDir.empty());

		// Promised, not done - which is the point: nothing may be written down as installed yet.
		TEST_ASSERT(!other.getChildFile("Thing.vst3").exists());

		// And what it promises is a real staging folder, so the copy can still be made afterwards.
		TEST_ASSERT(juce::File(juce::String(pending.job.sourceDir)).getChildFile("Thing.vst3").existsAsFile());

		// The files are named either way, because they are read out of the staging folder.
		TEST_ASSERT(files.size() == 1 && files.front() == "Thing.vst3");

		// And a batch that is cleared takes the staging folder with it, whether the copy was ever
		// made or not - otherwise an abandoned run leaves a plugin in the temp folder every time.
		const auto staging = juce::File(juce::String(pending.stagingDir));
		TEST_ASSERT(staging.isDirectory());

		ElevatedBatch batch;
		batch.add(pending, {}, files);
		batch.clear();

		TEST_ASSERT(!staging.exists());

		root.deleteRecursively();
	}

	// Last-Modified, which is the one thing a server says in a clock everybody shares. Read wrongly it
	// would not fail loudly - it would quietly hide every update there is - so the arithmetic is
	// pinned here against dates worked out elsewhere.
	// How long a request told 429 Too Many Requests waits before it asks again: what the server says,
	// whichever of its two forms it uses, else 30, 60 and 120 seconds, and never more than five minutes.
	void testARateLimitedRequestWaitsAsLongAsItIsTold()
	{
		const int64_t now = 1791225444;	// Mon, 05 Oct 2026 18:37:24 GMT

		TEST_ASSERT(HttpClient::retryDelaySeconds("120", 0, now) == 120);
		TEST_ASSERT(HttpClient::retryDelaySeconds(" 7 ", 2, now) == 7);
		TEST_ASSERT(HttpClient::retryDelaySeconds("Mon, 05 Oct 2026 18:38:54 GMT", 0, now) == 90);

		TEST_ASSERT(HttpClient::retryDelaySeconds("", 0, now) == 30);
		TEST_ASSERT(HttpClient::retryDelaySeconds("", 1, now) == 60);
		TEST_ASSERT(HttpClient::retryDelaySeconds("", 2, now) == 120);
		TEST_ASSERT(HttpClient::retryDelaySeconds("soon", 0, now) == 30);

		// A date already gone and a zero say nothing, so the own schedule applies.
		TEST_ASSERT(HttpClient::retryDelaySeconds("Mon, 05 Oct 2026 18:00:00 GMT", 1, now) == 60);
		TEST_ASSERT(HttpClient::retryDelaySeconds("0", 0, now) == 30);

		TEST_ASSERT(HttpClient::retryDelaySeconds("86400", 0, now) == 300);
	}

	void testHttpDatesBecomeSeconds()
	{
		int64_t t = -1;

		TEST_ASSERT(HttpClient::parseHttpDate("Thu, 01 Jan 1970 00:00:00 GMT", t) && t == 0);
		TEST_ASSERT(HttpClient::parseHttpDate("Mon, 05 Oct 2026 18:37:24 GMT", t) && t == 1791225444);

		// The leap day, and the day after a leap year - where a home-made calendar goes wrong first.
		TEST_ASSERT(HttpClient::parseHttpDate("Sat, 29 Feb 2020 12:00:00 GMT", t) && t == 1582977600);
		TEST_ASSERT(HttpClient::parseHttpDate("Tue, 01 Mar 2016 00:00:00 GMT", t) && t == 1456790400);

		// Later is bigger, which is the only property anything here actually uses.
		int64_t a = 0, b = 0;
		TEST_ASSERT(HttpClient::parseHttpDate("Mon, 05 Oct 2026 18:37:24 GMT", a));
		TEST_ASSERT(HttpClient::parseHttpDate("Tue, 06 Oct 2026 00:00:00 GMT", b));
		TEST_ASSERT(b > a);

		// Anything else is refused rather than guessed at.
		TEST_ASSERT(!HttpClient::parseHttpDate("", t));
		TEST_ASSERT(!HttpClient::parseHttpDate("yesterday", t));
		TEST_ASSERT(!HttpClient::parseHttpDate("Mon, 05 Xxx 2026 18:37:24 GMT", t));
	}

	// The date a protected channel prints beside each file, which is the only thing that tells one
	// build from the next when the version number stays where it was.
	void testADirectoryListingCarriesTheDate()
	{
		const std::string html =
			R"(<tr><td><a href="/b/TheUsualSuspects-Synth-1000-VST3-2.2.19-win64.zip">x</a></td>)"
			R"(<td>2026-10-05 18:37</td><td>7079k</td></tr>)"
			R"(<tr><td><a href="/b/TheUsualSuspects-Synth-1000-VST2-2.2.19-win64.zip">x</a></td>)"
			R"(<td>2026-09-01 09:00</td><td>7079k</td></tr>)";

		const auto assets = parseDirectoryIndex(html, "https://x/b/", Channel::Donators);

		TEST_ASSERT(assets.size() == 2);
		TEST_ASSERT(assets[0].listed == "2026-10-05 18:37");

		// Out of its own row: a file must not pick up the date of the one after it.
		TEST_ASSERT(assets[1].listed == "2026-09-01 09:00");

		// A listing without dates is not an error, it just leaves the field empty.
		const auto plain = parseDirectoryIndex(
			R"(<a href="/b/TheUsualSuspects-Synth-1000-VST3-2.2.19-win64.zip">x</a>)", "https://x/b/", Channel::Donators);

		TEST_ASSERT(plain.size() == 1 && plain[0].listed.empty());
	}

	// Same version, newer file. The whole point: a donator build ships under the number the last one
	// had, so without the date there is nothing to notice.
	void testABuildOfTheSameVersionCanStillBeNewer()
	{
		const Fixture fixture;

		auto pm = makeManager(true);

		std::string error;
		TEST_ASSERT(pm.refresh(error));

		Asset asset;
		asset.product = "Synth-1000";
		asset.format = "VST3";
		asset.version = "2.2.19";
		asset.channel = Channel::Donators;
		asset.listed = "2026-10-05 18:37";

		// Nothing installed: nothing to be newer than, whatever the date says.
		TEST_ASSERT(!pm.isNewerBuild(asset));

		auto installed = asset;
		installed.listed = "2026-09-01 09:00";
		pm.recordInstall(installed, {});

		TEST_ASSERT(pm.isNewerBuild(asset));

		// The one that is installed is not newer than itself, and neither is an older one.
		TEST_ASSERT(!pm.isNewerBuild(installed));

		auto older = asset;
		older.listed = "2026-08-01 09:00";
		TEST_ASSERT(!pm.isNewerBuild(older));

		// A public release has a version number for this and is never judged by a date.
		auto published = asset;
		published.channel = Channel::Public;
		TEST_ASSERT(!pm.isNewerBuild(published));

		// And a listing that printed no date leaves the version to decide, as before.
		auto undated = asset;
		undated.listed.clear();
		TEST_ASSERT(!pm.isNewerBuild(undated));
	}

	void testRefreshMergesChannels()
	{
		const Fixture fixture;

		auto pm = makeManager(true);

		std::string error;
		TEST_ASSERT(pm.refresh(error));
		TEST_ASSERT(error.empty());

		const auto& catalog = pm.getCatalog();

		// Two public products plus the two that only exist internally - a plugin and its test
		// console - and the public ones stay first.
		TEST_ASSERT(catalog.getProducts().size() == 4);
		TEST_ASSERT(catalog.getProducts()[0].product == "Synth-1000");
		TEST_ASSERT(catalog.getProducts()[3].product == "Prototype-9");

		const auto versions = catalog.getVersions("Synth-1000", "VST3", "windows", "x86_64");

		TEST_ASSERT(versions.size() == 2);
		TEST_ASSERT(versions[0] == "2.2.19");
		TEST_ASSERT(versions[1] == "2.2.16");

		TEST_ASSERT(catalog.getChannel("Synth-1000", "VST3", "2.2.19", "windows", "x86_64") == Channel::Internal);
		TEST_ASSERT(catalog.getChannel("Synth-1000", "VST3", "2.2.16", "windows", "x86_64") == Channel::Public);

		// The published build keeps the URL GitHub gave it, the internal one is built from the folder.
		TEST_ASSERT(catalog.find("Synth-1000", "VST3", "2.2.16", "windows", "x86_64")->url == "https://releases/s1000-vst3.zip");
		TEST_ASSERT(catalog.find("Synth-1000", "VST3", "2.2.19", "windows", "x86_64")->url ==
			urls().serverBuilds + "synth-1000/internal/TheUsualSuspects-Synth-1000-VST3-2.2.19-win64.zip");
	}

	// Without the team login none of that is visible - not the internal builds, and not even the
	// existence of a product that has no public release.
	// GitHub allows sixty requests an hour from an address, and a room of testers behind one
	// router shares that number. So the published builds are read from our own copy of what the
	// API answers, and GitHub is what happens when there is no copy to read.
	void testPublicBuildsPreferOurOwnCopy()
	{
		const Fixture fixture;

		// Nothing on GitHub to find. The catalog is built anyway, so the copy is what was read.
		urls().githubReleases = fixture.url("no-such-file.json");

		{
			auto pm = makeManager(false);

			std::string error;
			TEST_ASSERT(pm.refresh(error));
			TEST_ASSERT(error.empty());
			TEST_ASSERT(pm.getCatalog().find("Synth-1000", "VST3", "2.2.16", "windows", "x86_64"));
		}

		// And the other way round: no copy, so GitHub answers instead.
		urls().githubReleases = fixture.url("releases.json");
		urls().releaseCache = fixture.url("no-such-folder/");

		{
			auto pm = makeManager(false);

			std::string error;
			TEST_ASSERT(pm.refresh(error));
			TEST_ASSERT(error.empty());
			TEST_ASSERT(pm.getCatalog().find("Synth-1000", "VST3", "2.2.16", "windows", "x86_64"));
		}

		// A copy that names a version it does not hold is not a reason to offer nothing: what is
		// there is still read, and only an empty one sends this to GitHub.
		fixture.write("cache/versions.json", R"(["9.9.9","2.2.16"])");
		urls().releaseCache = fixture.url("cache/");
		urls().githubReleases = fixture.url("no-such-file.json");

		{
			auto pm = makeManager(false);

			std::string error;
			TEST_ASSERT(pm.refresh(error));
			TEST_ASSERT(pm.getCatalog().find("Synth-1000", "VST3", "2.2.16", "windows", "x86_64"));
		}

		// Neither source: that is worth reporting rather than showing an empty window.
		urls().releaseCache = fixture.url("no-such-folder/");

		{
			auto pm = makeManager(false);

			std::string error;
			TEST_ASSERT(!pm.refresh(error));
			TEST_ASSERT(error.find("GitHub") != std::string::npos);
		}
	}

	void testProtectedChannelsNeedALogin()
	{
		const Fixture fixture;

		auto pm = makeManager(false);

		std::string error;
		TEST_ASSERT(pm.refresh(error));

		TEST_ASSERT(pm.getCatalog().getProducts().size() == 2);
		TEST_ASSERT(!pm.getCatalog().findProduct("Prototype-9"));

		const auto versions = pm.getCatalog().getVersions("Synth-1000", "VST3", "windows", "x86_64");

		TEST_ASSERT(versions.size() == 1);
		TEST_ASSERT(versions[0] == "2.2.16");
	}

	// The three ways a request for a protected channel can fail have three different answers for
	// the user, and all three arrive as the same kind of string. A 404 counted as a connection
	// failure is what sent a donator login looking at their network, when what the server had said
	// was that the folder they named holds nothing.
	void testHttpFailuresAreToldApart()
	{
		TEST_ASSERT(HttpClient::isAuthError("HTTP 401"));
		TEST_ASSERT(HttpClient::isAuthError("HTTP 403"));
		TEST_ASSERT(!HttpClient::isAuthError("HTTP 404"));

		TEST_ASSERT(HttpClient::isNotFound("HTTP 404"));
		TEST_ASSERT(!HttpClient::isNotFound("HTTP 401"));
		TEST_ASSERT(!HttpClient::isNotFound("HTTP 500"));

		// Neither of them, so the message stays the one about the server itself.
		TEST_ASSERT(!HttpClient::isAuthError("Failed to connect"));
		TEST_ASSERT(!HttpClient::isNotFound("Failed to connect"));
	}

	// A donator folder that is there but holds nothing. The server accepts the login and answers, so
	// the request succeeds - but a product nobody has published reaches the catalog only through the
	// builds in its folder, so an empty one would store a login and then show nothing for it.
	void testALoginForAnEmptyFolderIsRefused()
	{
		const Fixture fixture;

		// A login that is kept is written to disk, so the settings go somewhere of their own rather
		// than into whatever this machine's own plugin manager keeps.
		const auto settings = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("pluginManagerTest-login-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

		TEST_ASSERT(settings.createDirectory().wasOk());

		PlatformUtils::setSettingsFolder(settings.getFullPathName().toStdString());

		{
			fixture.write("builds/not-uploaded-yet/donators/index.html", "<html><body></body></html>");

			PluginManager pm(false);

			std::string error;
			TEST_ASSERT(!pm.login(Channel::Donators, "not-uploaded-yet", {"user", "password"}, error));

			// Named, so that it is clear which folder was looked in - and not kept, so that nothing
			// claims a login for a product the list does not hold.
			TEST_ASSERT(error.find("not-uploaded-yet") != std::string::npos);
			TEST_ASSERT(!pm.isLoggedIn(Channel::Donators, "not-uploaded-yet"));

			// The same folder with a build in it is a login worth keeping.
			fixture.write("builds/not-uploaded-yet/donators/index.html",
				R"(<a href="TheUsualSuspects-Not-Uploaded-Yet-VST3-2.2.19-win64.zip">x</a>)");

			TEST_ASSERT(pm.login(Channel::Donators, "not-uploaded-yet", {"user", "password"}, error));
			TEST_ASSERT(pm.isLoggedIn(Channel::Donators, "not-uploaded-yet"));
		}

		PlatformUtils::setSettingsFolder({});
		settings.deleteRecursively();
	}

	// A plugin nobody has published is in no product list, so a donator login builds it from the
	// names of the builds in its folder - which gives a bare name and no picture. A products.json
	// beside those builds is how the folder says what it actually is.
	void testADonatorFolderCanDescribeItself()
	{
		const Fixture fixture;

		fixture.write("builds/prototype-9/donators/index.html",
			R"(<a href="TheUsualSuspects-Prototype-9-VST3-2.2.19-win64.zip">x</a>)");

		auto pm = makeManager(false);
		pm.setCredentials(Channel::Donators, "prototype-9", {"user", "password"});

		std::string error;
		TEST_ASSERT(pm.refresh(error));

		// Without the file: named after the builds, and nothing to show for it.
		{
			const auto* product = pm.getCatalog().findProduct("Prototype-9");

			TEST_ASSERT(product);
			TEST_ASSERT(product->name == "Prototype-9");
			TEST_ASSERT(product->thumbnail.empty());
		}

		fixture.write("builds/prototype-9/donators/products.json", R"({"schemaVersion":2,"products":[
			{"slug":"prototype-9","product":"Prototype-9","name":"Prototype 9",
			 "description":"Some Hardware Emulator",
			 "thumbnail":"https://dsp56300.com/builds/prototype-9/donators/Prototype-9_thumbnail.png"}]})");

		// The folder's listing shows it, as the server's does: only a folder that has one is asked for it.
		fixture.write("builds/prototype-9/donators/index.html",
			R"(<a href="TheUsualSuspects-Prototype-9-VST3-2.2.19-win64.zip">x</a><a href="products.json">x</a>)");

		TEST_ASSERT(pm.refresh(error));

		{
			const auto* product = pm.getCatalog().findProduct("Prototype-9");

			TEST_ASSERT(product);
			TEST_ASSERT(product->name == "Prototype 9");
			TEST_ASSERT(product->description == "Some Hardware Emulator");
			TEST_ASSERT(!product->thumbnail.empty());

			// Taken from the folder that was opened and from the builds in it, never from the file:
			// a mistake in there must not leave a row that matches none of them.
			TEST_ASSERT(product->slug == "prototype-9");
			TEST_ASSERT(product->product == "Prototype-9");

			// And the build is still offered, so the file in the folder was not mistaken for one.
			TEST_ASSERT(pm.getCatalog().getVersions("Prototype-9", "VST3", "windows", "x86_64").size() == 1);
		}
	}

	// A login is sent to our own build server and nowhere else. The published products keep their
	// pictures on the website, and that host has no business seeing an internal password.
	void testAThumbnailLoginGoesOnlyToOurServer()
	{
		const Fixture fixture;

		auto pm = makeManager(true);
		pm.setCredentials(Channel::Donators, "prototype-9", {"donator", "secret"});

		Product onTheWebsite;
		onTheWebsite.slug = "prototype-9";
		onTheWebsite.thumbnail = "https://theusualsuspects.io/installer/Osirus_thumbnail.png";

		TEST_ASSERT(!pm.credentialsForThumbnail(onTheWebsite).valid());

		Product besideItsBuilds;
		besideItsBuilds.slug = "prototype-9";
		besideItsBuilds.thumbnail = urls().serverBuilds + "prototype-9/donators/x.png";

		TEST_ASSERT(pm.credentialsForThumbnail(besideItsBuilds).user == "donator");

		// An internal folder takes the team login, even for a product this user also has a donator
		// login for - the folder decides, not what happens to be in the store.
		Product internalFolder;
		internalFolder.slug = "prototype-9";
		internalFolder.thumbnail = urls().serverBuilds + "prototype-9/internal/x.png";

		TEST_ASSERT(pm.credentialsForThumbnail(internalFolder).user == "team");

		// The server the builds were on before is ours too, for what a product list still points at there.
		Product onThePreviousServer;
		onThePreviousServer.slug = "prototype-9";
		onThePreviousServer.thumbnail = urls().previousServerBuilds + "prototype-9/internal/x.png";

		TEST_ASSERT(pm.credentialsForThumbnail(onThePreviousServer).user == "team");

		// Nothing to send it to.
		TEST_ASSERT(!pm.credentialsForThumbnail({}).valid());
	}

	// One source being unreachable must not throw away the others: a refresh that cannot read the
	// product list still offers the builds, and says what failed.
	void testRefreshSurvivesAMissingSource()
	{
		const Fixture fixture;

		urls().publicProducts = fixture.url("no-such-file.json");

		auto pm = makeManager(false);

		std::string error;
		TEST_ASSERT(!pm.refresh(error));
		TEST_ASSERT(!error.empty());

		TEST_ASSERT(pm.getCatalog().getProducts().empty());
		TEST_ASSERT(pm.getCatalog().getVersions("Synth-1000", "VST3", "windows", "x86_64").size() == 1);
	}

	// An effect build does not have to exist for every version of its plugin. JE-8086 gets one at
	// 2.2.26 and has none before that, so a row can offer a version with an effect build and a
	// version without, and the answer has to be per version rather than per product.
	void testAnEffectBuildCanExistForOnlySomeVersions()
	{
		const Fixture fixture;

		auto pm = makeManager(true);

		std::string error;
		TEST_ASSERT(pm.refresh(error));

		const auto& catalog = pm.getCatalog();

		// Two versions of the plugin, and the effect build only ships with the newer one.
		const auto versions = catalog.getVersions("Synth-1000", "VST3", "windows", "x86_64");

		TEST_ASSERT(versions.size() == 2);
		TEST_ASSERT(versions[0] == "2.2.19" && versions[1] == "2.2.16");

		// The product has one, which is what puts the chip in the row at all.
		TEST_ASSERT(catalog.fxProductOf("Synth-1000") == "Synth-1000FX");

		// But only for the version that ships it. This is what the row asks before it offers the
		// effect build or puts one in the download queue, so a wrong answer here would either
		// fetch a build that does not exist or quietly skip one that does.
		TEST_ASSERT(catalog.find("Synth-1000FX", "VST3", "2.2.19", "windows", "x86_64"));
		TEST_ASSERT(!catalog.find("Synth-1000FX", "VST3", "2.2.16", "windows", "x86_64"));

		// And the plugin itself is offered in both, so the version without an effect build is not
		// hidden or made unusable by the one with it.
		TEST_ASSERT(catalog.find("Synth-1000", "VST3", "2.2.19", "windows", "x86_64"));
		TEST_ASSERT(catalog.find("Synth-1000", "VST3", "2.2.16", "windows", "x86_64"));

		// The effect build belongs to the plugin whichever version it came with, so it is installed
		// where the plugin is and fetched with the plugin's login.
		const auto* owner = catalog.findOwningProduct("Synth-1000FX");

		TEST_ASSERT(owner && owner->product == "Synth-1000");
	}

	// An effect build is its own product in the file names but has no location of its own: it is
	// installed beside the plugin it extends, so everything that looks for it afterwards has to
	// look there too - or it looks in a folder nothing ever put it in.
	void testEffectBuildsFollowTheirPlugin()
	{
		const Fixture fixture;

		const auto settings = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("pluginManagerTest-fxpath-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

		TEST_ASSERT(settings.createDirectory().wasOk());

		PlatformUtils::setSettingsFolder(settings.getFullPathName().toStdString());

		{
			auto pm = makeManager(true);

			std::string error;
			TEST_ASSERT(pm.refresh(error));

			TEST_ASSERT(pm.getCatalog().fxProductOf("Synth-1000") == "Synth-1000FX");
			TEST_ASSERT(pm.getCatalog().fxProductOf("Prototype-9").empty());

			pm.getInstallPaths().set(PluginFormat::VST3, "/all/vst3");

			TEST_ASSERT(pm.getInstallPath("Synth-1000FX", "VST3") == "/all/vst3");

			// Moving the plugin moves the effect build, although nothing named it.
			pm.setInstallPath("Synth-1000", "VST3", "/just/this/one");

			TEST_ASSERT(pm.getInstallPath("Synth-1000FX", "VST3") == "/just/this/one");

			// And where an install of it would go, which is the same answer: before this, an install
			// put it beside the plugin while everything that looked for it afterwards looked in the
			// folder the format points at for everything else.
			Asset fx;
			TEST_ASSERT(parseAssetName("TheUsualSuspects-Synth-1000FX-VST3-2.2.19-win64.zip", fx));
			TEST_ASSERT(pm.installPathFor(fx) == "/just/this/one");

			// Pointing the effect build somewhere points its plugin there: it has no location of its
			// own to point.
			pm.setInstallPath("Synth-1000FX", "VST3", "/somewhere/else");

			TEST_ASSERT(pm.getInstallPath("Synth-1000", "VST3") == "/somewhere/else");
		}

		PlatformUtils::setSettingsFolder({});
		settings.deleteRecursively();
	}

	// An FX build is its own product in the file names but ships from the base product's folder, so it
	// is the base product's login that downloads it.
	void testCredentialsFollowTheOwningProduct()
	{
		const Fixture fixture;

		auto pm = makeManager(true);

		std::string error;
		TEST_ASSERT(pm.refresh(error));

		const auto* fx = pm.getCatalog().find("Synth-1000FX", "VST3", "2.2.19", "windows", "x86_64");
		TEST_ASSERT(fx);
		TEST_ASSERT(pm.credentialsFor(*fx).user == "team");

		// A published build is downloaded without one.
		const auto* published = pm.getCatalog().find("Synth-1000", "VST3", "2.2.16", "windows", "x86_64");
		TEST_ASSERT(published);
		TEST_ASSERT(!pm.credentialsFor(*published).valid());
	}

	// A test console is a product of its own in the team's list, with no folder of its own, while its
	// donator builds sit in the folder of its plugin. That folder's login is the one that downloads
	// them: asking for the console's own login sent none, and the server answered 401.
	void testDonatorCredentialsFollowTheFolder()
	{
		const Fixture fixture;

		auto pm = makeManager(true);

		std::string error;
		TEST_ASSERT(pm.refresh(error));

		pm.setCredentials(Channel::Donators, "synth-1000", {"donator", "secret"});

		Asset console;
		TEST_ASSERT(parseAssetName("TheUsualSuspects-Synth-1000TestConsole-2.2.19-win64.zip", console));
		console.channel = Channel::Donators;
		console.url = "https://server.test/builds/synth-1000/donators/TheUsualSuspects-Synth-1000TestConsole-2.2.19-win64.zip";

		const auto* owner = pm.getCatalog().findOwningProduct(console.product);
		TEST_ASSERT(owner && owner->product == "Synth-1000TestConsole");
		TEST_ASSERT(pm.credentialsFor(console).user == "donator");

		// The same build in another product's folder gets that folder's login, here none.
		console.url = "https://server.test/builds/prototype-9/donators/TheUsualSuspects-Synth-1000TestConsole-2.2.19-win64.zip";
		TEST_ASSERT(!pm.credentialsFor(console).valid());
	}

	// A catalog may arrive compressed, because the request offers to take it that way. What came
	// back is told apart by what it is, so a server that ignores the offer is read just the same.
	void testCompressedBodiesAreRead()
	{
		const std::string plain = R"({"products":[{"product":"Synth-1000","name":"Synth 1000"}]})";

		TEST_ASSERT(HttpClient::decodeBody(plain.data(), plain.size()) == plain);

		juce::MemoryOutputStream compressed;

		{
			juce::GZIPCompressorOutputStream gzip(compressed, 9,
				juce::GZIPCompressorOutputStream::windowBitsGZIP);

			TEST_ASSERT(gzip.write(plain.data(), plain.size()));
		}

		// Really compressed, and really read back.
		TEST_ASSERT(compressed.getDataSize() != plain.size());
		TEST_ASSERT(HttpClient::decodeBody(compressed.getData(), compressed.getDataSize()) == plain);

		// Nothing at all is not a failure, it is nothing at all.
		TEST_ASSERT(HttpClient::decodeBody(nullptr, 0).empty());
		TEST_ASSERT(HttpClient::decodeBody(plain.data(), 0).empty());

		// One byte cannot say it is gzip, and must not be mistaken for it.
		TEST_ASSERT(HttpClient::decodeBody("x", 1) == "x");
	}

	void testDownloadWritesTheFile()
	{
		const Fixture fixture;

		const auto target = fixture.dir.getChildFile("downloaded.json");

		std::string error;
		TEST_ASSERT(HttpClient::downloadFile(fixture.url("products.json"), target.getFullPathName().toStdString(), error));
		TEST_ASSERT(target.existsAsFile());
		TEST_ASSERT(target.loadFileAsString().contains("Synth-1000"));

		// A download that fails leaves nothing behind that could pass for the real thing.
		const auto missing = fixture.dir.getChildFile("missing.zip");
		TEST_ASSERT(!HttpClient::downloadFile(fixture.url("nope.zip"), missing.getFullPathName().toStdString(), error));
		TEST_ASSERT(!missing.exists());
		TEST_ASSERT(!error.empty());
	}

	// Where a format is installed is a setting, and the one the headless program reads too, so the
	// override rules are worth pinning down. Nothing here is written to disk.
	void testInstallPathOverrides()
	{
		InstallPaths paths(false);

		const auto defaultVst3 = InstallPaths::getDefault(PluginFormat::VST3);

		TEST_ASSERT(paths.get(PluginFormat::VST3) == defaultVst3);
		TEST_ASSERT(!paths.isCustom(PluginFormat::VST3));

		paths.set(PluginFormat::VST3, "/somewhere/else");

		TEST_ASSERT(paths.get(PluginFormat::VST3) == "/somewhere/else");
		TEST_ASSERT(paths.isCustom(PluginFormat::VST3));

		// Only the format that was set moves.
		TEST_ASSERT(paths.get(PluginFormat::CLAP) == InstallPaths::getDefault(PluginFormat::CLAP));

		// An empty location is what the Default button sends, and so is the default itself: neither
		// leaves an override behind that would pin the location if the platform's default changes.
		paths.set(PluginFormat::VST3, {});
		TEST_ASSERT(!paths.isCustom(PluginFormat::VST3));

		paths.set(PluginFormat::VST3, defaultVst3);
		TEST_ASSERT(!paths.isCustom(PluginFormat::VST3));
		TEST_ASSERT(paths.get(PluginFormat::VST3) == defaultVst3);
	}

	// Standalone has no install location until one is set, which is what makes it the one format the
	// preferences page cannot leave empty.
	// Every format this system can hold a plugin in, which is not every format the program knows:
	// there is no AU off macOS, and offering somewhere to put one is a setting that answers nothing.
	void testFormatsAreTheOnesThisSystemHas()
	{
		const auto formats = InstallPaths::getFormats();

		const auto has = [&](const PluginFormat _format)
		{
			return std::find(formats.begin(), formats.end(), _format) != formats.end();
		};

		// Everywhere.
		TEST_ASSERT(has(PluginFormat::VST2));
		TEST_ASSERT(has(PluginFormat::VST3));
		TEST_ASSERT(has(PluginFormat::CLAP));
		TEST_ASSERT(has(PluginFormat::LV2));

		// Nowhere by default, but a location can be set for it - see testStandaloneHasNoDefault.
		TEST_ASSERT(has(PluginFormat::Standalone));

#if JUCE_MAC
		TEST_ASSERT(has(PluginFormat::AU));
#else
		TEST_ASSERT(!has(PluginFormat::AU));
#endif
	}

	// It used to have none, and then nothing could be installed in it until somebody typed a path -
	// which is the one format the internal channel adds over the public one.
	void testEveryFormatHasSomewhereToGo()
	{
		InstallPaths paths(false);

		for (const auto format : InstallPaths::getFormats())
		{
			const auto path = paths.get(format);

			TEST_ASSERT(!path.empty());
			TEST_ASSERT(!paths.isCustom(format));

			// Somewhere real, not a name on its own: every one of these hangs off a folder the
			// system told us about.
			TEST_ASSERT(juce::File::isAbsolutePath(juce::String(path)));
		}

		// And what is set by hand is what is used, default or no default.
		paths.set(PluginFormat::Standalone, "/opt/tus");

		TEST_ASSERT(paths.get(PluginFormat::Standalone) == "/opt/tus");
		TEST_ASSERT(paths.isCustom(PluginFormat::Standalone));

		// Emptied again, it goes back to the default rather than to nothing.
		paths.set(PluginFormat::Standalone, {});

		TEST_ASSERT(!paths.get(PluginFormat::Standalone).empty());
		TEST_ASSERT(!paths.isCustom(PluginFormat::Standalone));
	}

	// What the detail page shows as the location, and what decides whether a plugin counts as
	// installed there.
	// A failed install used to be a silent no-op. What the user is told now comes from here, so it
	// has to be both true and specific.
	// What this program installed is written down, because most formats cannot be asked. The entry
	// only counts while the file it describes is still there.
	// What is installed can also be asked of the file itself, for anything this program did not put
	// there. On Windows that means the version resource in the binary, which for a VST3 is inside
	// the bundle. An LV2 is answered from its turtle instead, on every system - see
	// testAnLv2SaysWhichVersionItIs.
	//
	// This program's own executable carries such a resource, so it stands in for a plugin here.
	void testVersionIsReadFromInsideABundle()
	{
#if JUCE_WINDOWS
		const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("pluginManagerTest-bundle-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

		const auto me = juce::File::getSpecialLocation(juce::File::currentExecutableFile);

		// An LV2 bundle is a folder, so it counts as installed by being there at all - what version
		// it is comes from the turtle in it rather than from any library beside it.
		const auto lv2 = dir.getChildFile("Synth-1000.lv2");

		TEST_ASSERT(lv2.createDirectory().wasOk());
		TEST_ASSERT(lv2.getChildFile("manifest.ttl").replaceWithText("@prefix lv2:"));

		const auto path = dir.getFullPathName().toStdString();

		TEST_ASSERT(PlatformUtils::isPluginInstalled("Synth-1000", PluginFormat::LV2, path));

		// A VST3 bundle keeps it under Contents instead, named after the bundle itself.
		const auto vst3 = dir.getChildFile("Synth-1000.vst3/Contents/x86_64-win");

		TEST_ASSERT(vst3.createDirectory().wasOk());
		TEST_ASSERT(me.copyFileTo(vst3.getChildFile("Synth-1000.vst3")));

		TEST_ASSERT(!PlatformUtils::getInstalledVersion("Synth-1000", PluginFormat::VST3, path).empty());

		// And a bundle with nothing in it that could say is not a version of nothing.
		const auto empty = dir.getChildFile("Synth-2000.lv2");

		TEST_ASSERT(empty.createDirectory().wasOk());
		TEST_ASSERT(empty.getChildFile("manifest.ttl").replaceWithText("@prefix lv2:"));

		TEST_ASSERT(PlatformUtils::isPluginInstalled("Synth-2000", PluginFormat::LV2, path));
		TEST_ASSERT(PlatformUtils::getInstalledVersion("Synth-2000", PluginFormat::LV2, path).empty());

		dir.deleteRecursively();
#endif
	}

	void testInstalledVersionsAreRemembered()
	{
		const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("pluginManagerTest-installed-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

		TEST_ASSERT(dir.createDirectory().wasOk());

		const auto path = dir.getFullPathName().toStdString();

		Asset asset;
		TEST_ASSERT(parseAssetName("TheUsualSuspects-Synth-1000-VST3-2.2.19-win64.zip", asset));

		auto installedFile = juce::File(juce::String(PlatformUtils::installedFilePath("Synth-1000", PluginFormat::VST3, path)));
		TEST_ASSERT(installedFile.create().wasOk());

		InstalledPlugins installed(false);

		// Nothing recorded yet, and the file itself does not say - it is not a real plugin.
		TEST_ASSERT(installed.getVersion("Synth-1000", PluginFormat::VST3, path).empty());

		installed.set(asset, path);

		TEST_ASSERT(installed.getVersion("Synth-1000", PluginFormat::VST3, path) == "2.2.19");

		// Another product, another format and another location are all different things.
		TEST_ASSERT(installed.getVersion("Rack-A-100", PluginFormat::VST3, path).empty());
		TEST_ASSERT(installed.getVersion("Synth-1000", PluginFormat::CLAP, path).empty());
		TEST_ASSERT(installed.getVersion("Synth-1000", PluginFormat::VST3, dir.getParentDirectory().getFullPathName().toStdString()).empty());

		// A record of something that is no longer there says nothing.
		installedFile.deleteRecursively();
		TEST_ASSERT(installed.getVersion("Synth-1000", PluginFormat::VST3, path).empty());

		dir.deleteRecursively();
	}


	// A product can be pointed somewhere of its own, which wins over where the format goes, which in
	// turn wins over what the platform expects.
	void testPerProductPathsWin()
	{
		InstallPaths paths(false);

		const auto platform = InstallPaths::getDefault(PluginFormat::VST3);

		TEST_ASSERT(paths.get("Synth-1000", PluginFormat::VST3) == platform);
		TEST_ASSERT(!paths.isCustom("Synth-1000", PluginFormat::VST3));

		paths.set(PluginFormat::VST3, "/all/vst3");

		TEST_ASSERT(paths.get("Synth-1000", PluginFormat::VST3) == "/all/vst3");
		TEST_ASSERT(!paths.isCustom("Synth-1000", PluginFormat::VST3));		// it follows the format

		paths.set("Synth-1000", PluginFormat::VST3, "/just/this/one");

		TEST_ASSERT(paths.get("Synth-1000", PluginFormat::VST3) == "/just/this/one");
		TEST_ASSERT(paths.isCustom("Synth-1000", PluginFormat::VST3));

		// Only that product and that format moved.
		TEST_ASSERT(paths.get("Rack-A-100", PluginFormat::VST3) == "/all/vst3");
		TEST_ASSERT(paths.get("Synth-1000", PluginFormat::CLAP) == InstallPaths::getDefault(PluginFormat::CLAP));

		// Emptying it puts the product back to wherever the format goes, and so does naming the very
		// place the format already points at.
		paths.set("Synth-1000", PluginFormat::VST3, {});
		TEST_ASSERT(!paths.isCustom("Synth-1000", PluginFormat::VST3));

		paths.set("Synth-1000", PluginFormat::VST3, "/all/vst3");
		TEST_ASSERT(!paths.isCustom("Synth-1000", PluginFormat::VST3));
	}



	// An archive does not have to be shaped like the folder it is installed into. The Linux builds
	// carry the plugin under usr/local/lib/<format>, and unpacked at face value it lands there -
	// where no host ever looks - so what is installed is the folder the plugin is in.
	// A program is not named after its product - OsirusTestConsole ships virusTestConsole - so
	// there is no name to find it by inside the archive, and on Linux every build is packed under
	// usr/local. Without stripping that, installing one puts a usr/local tree in the install
	// folder and the program itself three folders inside it.
	void testAProgramIsFoundWithoutKnowingItsName()
	{
		const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("pluginManagerTest-program-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

		const auto content = root.getChildFile("content");
		const auto target = root.getChildFile("programs");

		// What the Linux test console archive holds, names and all.
		TEST_ASSERT(content.getChildFile("usr/local").createDirectory().wasOk());
		TEST_ASSERT(content.getChildFile("usr/local/virusTestConsole").replaceWithText("program"));
		TEST_ASSERT(content.getChildFile("usr/local/start_IndiArp_BC.sh").replaceWithText("run"));

		const auto zip = root.getChildFile("console.zip");

		makeZip(content, zip);

		std::vector<std::string> files;
		std::string error;

		// Nothing to look for: the name the product would have is not in there.
		TEST_ASSERT(ZipExtractor::install(zip.getFullPathName().toStdString(),
			target.getFullPathName().toStdString(), "OsirusTestConsole.exe", "OsirusTestConsole", false, files, error));

		TEST_ASSERT(target.getChildFile("virusTestConsole").existsAsFile());
		TEST_ASSERT(target.getChildFile("start_IndiArp_BC.sh").existsAsFile());
		TEST_ASSERT(!target.getChildFile("usr").exists());

		// And what came with it is written down under the names it has in the folder, or an
		// uninstall would look for usr/local/virusTestConsole and find nothing.
		TEST_ASSERT(std::find(files.begin(), files.end(), "virusTestConsole") != files.end());

		// A folder that is the thing being installed rather than a wrapper is not stepped into,
		// which is what keeps a macOS bundle whole when its archive holds nothing else.
		const auto bundled = root.getChildFile("bundled");
		const auto bundleTarget = root.getChildFile("programs2");

		TEST_ASSERT(bundled.getChildFile("Thing.app/Contents/MacOS").createDirectory().wasOk());
		TEST_ASSERT(bundled.getChildFile("Thing.app/Contents/MacOS/Thing").replaceWithText("program"));

		const auto bundleZip = root.getChildFile("bundle.zip");

		makeZip(bundled, bundleZip);

		TEST_ASSERT(ZipExtractor::install(bundleZip.getFullPathName().toStdString(),
			bundleTarget.getFullPathName().toStdString(), "NotInHere.app", "NotInHere", false, files, error));

		TEST_ASSERT(bundleTarget.getChildFile("Thing.app/Contents/MacOS/Thing").existsAsFile());

		root.deleteRecursively();
	}

	// The team login survives a save, is not written twice, and does not drag anything else out
	// of the file with it. It used to ride along in the unknown-element passthrough; save()
	// writes it itself now, so both halves of that need pinning.
	void testTheTeamLoginFlagSurvivesBeingWrittenBack()
	{
		const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("tus_pm_test_teamlogin");

		root.deleteRecursively();
		TEST_ASSERT(root.createDirectory().wasOk());
		PlatformUtils::setSettingsFolder(root.getFullPathName().toStdString());

		const auto file = root.getChildFile("settings.xml");

		// Off until somebody says otherwise, and off is what its absence means - so a file written
		// while it is off must not mention it at all.
		{
			InstallPaths paths;

			TEST_ASSERT(!paths.hasTeamLogin());

			paths.set(PluginFormat::VST3, "/somewhere");
			paths.save();
		}

		TEST_ASSERT(!file.loadFileAsString().contains("teamLogin"));

		// Something this program does not understand, which has to outlive every write.
		{
			auto text = file.loadFileAsString();
			TEST_ASSERT(file.replaceWithText(text.replace("</pluginManager>",
				"  <somethingElse keep=\"yes\"/>\n</pluginManager>")));
		}

		{
			InstallPaths paths;

			paths.setTeamLogin(true);
			paths.save();
		}

		// Read back by a fresh instance, which is what a restart is.
		{
			InstallPaths paths;

			TEST_ASSERT(paths.hasTeamLogin());
			TEST_ASSERT(paths.get(PluginFormat::VST3) == "/somewhere");

			// Written again, by something that knows nothing about the flag.
			paths.set(PluginFormat::CLAP, "/elsewhere");
			paths.save();
		}

		const auto text = file.loadFileAsString();

		// Once, not twice: it is written explicitly now and must not also come through the
		// passthrough that used to carry it.
		TEST_ASSERT(text.indexOf("teamLogin") == text.lastIndexOf("teamLogin"));
		TEST_ASSERT(text.contains("somethingElse"));

		{
			InstallPaths paths;

			TEST_ASSERT(paths.hasTeamLogin());

			paths.setTeamLogin(false);
			paths.save();
		}

		TEST_ASSERT(!file.loadFileAsString().contains("teamLogin"));
		TEST_ASSERT(file.loadFileAsString().contains("somethingElse"));

		PlatformUtils::setSettingsFolder({});
		root.deleteRecursively();
	}

	// A backup is of the folder a product keeps for itself, and a restore puts it back. The
	// plugin is deliberately not in it: that can be downloaded again, and the data folder is the
	// half that cannot.
	void testBackupAndRestoreOfADataFolder()
	{
		const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("tus_pm_test_backup");

		root.deleteRecursively();
		TEST_ASSERT(root.createDirectory().wasOk());

		const auto data = root.getChildFile("data");
		setDataFolder(data.getFullPathName().toStdString());
		PlatformUtils::setSettingsFolder(root.getChildFile("settings").getFullPathName().toStdString());

		// The backups belong under the settings folder, so that --settings takes them with it.
		TEST_ASSERT(juce::File(juce::String(Backup::folder())).isAChildOf(root));

		const auto product = data.getChildFile("The Usual Suspects/Thing");

		std::string error;
		std::string file;

		// Nothing there yet, so there is nothing to promise anybody.
		TEST_ASSERT(!Backup::create("Thing", file, error));
		TEST_ASSERT(Backup::list("Thing").empty());

		TEST_ASSERT(product.getChildFile("config").createDirectory().wasOk());
		TEST_ASSERT(product.getChildFile("config/Thing.xml").replaceWithText("settings"));
		TEST_ASSERT(product.getChildFile("patchmanager").createDirectory().wasOk());
		TEST_ASSERT(product.getChildFile("patchmanager/db.json").replaceWithText("patches"));
		TEST_ASSERT(product.getChildFile("roms").createDirectory().wasOk());
		TEST_ASSERT(product.getChildFile("roms/thing.bin").replaceWithText("a rom"));

		TEST_ASSERT(Backup::sizeOf("Thing") > 0);
		TEST_ASSERT(Backup::create("Thing", file, error));
		TEST_ASSERT(juce::File(juce::String(file)).existsAsFile());

		auto backups = Backup::list("Thing");

		TEST_ASSERT(backups.size() == 1);
		TEST_ASSERT(backups.front().product == "Thing");
		TEST_ASSERT(backups.front().file == file);

		// Now break it the two ways a user can: change one file and lose another.
		TEST_ASSERT(product.getChildFile("config/Thing.xml").replaceWithText("broken"));
		TEST_ASSERT(product.getChildFile("roms/thing.bin").deleteFile());

		// ...and something added afterwards, which a restore must not take away.
		TEST_ASSERT(product.getChildFile("roms/another.bin").replaceWithText("added later"));

		TEST_ASSERT(Backup::restore(file, "Thing", error));

		TEST_ASSERT(product.getChildFile("config/Thing.xml").loadFileAsString() == "settings");
		TEST_ASSERT(product.getChildFile("patchmanager/db.json").loadFileAsString() == "patches");
		TEST_ASSERT(product.getChildFile("roms/thing.bin").loadFileAsString() == "a rom");
		TEST_ASSERT(product.getChildFile("roms/another.bin").loadFileAsString() == "added later");

		// And the state it wrote over was kept, so restoring the wrong one is survivable.
		backups = Backup::list("Thing");

		TEST_ASSERT(backups.size() == 2);

		const auto safety = std::find_if(backups.begin(), backups.end(), [&](const Backup::Entry& _e)
		{
			return _e.file != file;
		});

		TEST_ASSERT(safety != backups.end());

		// Deleting is only ever out of our own folder.
		TEST_ASSERT(!Backup::remove(product.getChildFile("config/Thing.xml").getFullPathName().toStdString(), error));
		TEST_ASSERT(product.getChildFile("config/Thing.xml").existsAsFile());

		TEST_ASSERT(Backup::remove(file, error));
		TEST_ASSERT(Backup::list("Thing").size() == 1);

		PlatformUtils::setSettingsFolder({});
		setDataFolder({});
		root.deleteRecursively();
	}

	// The backup folder can be pointed somewhere else - a folder a cloud service synchronises is
	// the reason to - and everything that makes, lists or deletes a backup follows it, the headless
	// program included, because it is read from the settings file rather than handed around.
	void testBackupsGoWhereTheyArePointed()
	{
		const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("tus_pm_test_backup_folder");

		root.deleteRecursively();
		TEST_ASSERT(root.createDirectory().wasOk());

		const auto data = root.getChildFile("data");
		setDataFolder(data.getFullPathName().toStdString());
		PlatformUtils::setSettingsFolder(root.getChildFile("settings").getFullPathName().toStdString());

		const auto product = data.getChildFile("The Usual Suspects/Thing");
		TEST_ASSERT(product.getChildFile("config").createDirectory().wasOk());
		TEST_ASSERT(product.getChildFile("config/Thing.xml").replaceWithText("settings"));

		const auto defaultFolder = juce::File(juce::String(InstallPaths::getDefaultBackupFolder()));
		const auto cloud = root.getChildFile("Dropbox/TUS backups");

		std::string error;
		std::string atDefault;
		TEST_ASSERT(Backup::create("Thing", atDefault, error));
		TEST_ASSERT(juce::File(juce::String(atDefault)).isAChildOf(defaultFolder));

		{
			InstallPaths paths;
			TEST_ASSERT(!paths.isCustomBackupFolder());
			paths.setBackupFolder(cloud.getFullPathName().toStdString());
			paths.save();
		}

		// Kept across a reload, and in use from then on. The one made before stays where it was
		// made, which is also why it is not listed: it is not in the folder backups are in.
		TEST_ASSERT(InstallPaths().isCustomBackupFolder());
		TEST_ASSERT(juce::File(juce::String(Backup::folder())) == cloud);
		TEST_ASSERT(Backup::list("Thing").empty());

		std::string inCloud;
		TEST_ASSERT(Backup::create("Thing", inCloud, error));
		TEST_ASSERT(juce::File(juce::String(inCloud)).isAChildOf(cloud));
		TEST_ASSERT(Backup::list("Thing").size() == 1);
		TEST_ASSERT(juce::File(juce::String(atDefault)).existsAsFile());

		// Deleting follows it too: out of the new folder yes, out of the old one no longer.
		TEST_ASSERT(!Backup::remove(atDefault, error));
		TEST_ASSERT(Backup::remove(inCloud, error));
		TEST_ASSERT(Backup::list("Thing").empty());

		// The default, set by hand or by emptying the field, is not remembered as a choice.
		{
			InstallPaths paths;
			paths.setBackupFolder(InstallPaths::getDefaultBackupFolder());
			TEST_ASSERT(!paths.isCustomBackupFolder());
			paths.setBackupFolder(cloud.getFullPathName().toStdString());
			paths.setBackupFolder({});
			TEST_ASSERT(!paths.isCustomBackupFolder());
			paths.save();
		}

		TEST_ASSERT(juce::File(juce::String(Backup::folder())) == defaultFolder);
		TEST_ASSERT(Backup::list("Thing").size() == 1);

		PlatformUtils::setSettingsFolder({});
		setDataFolder({});
		root.deleteRecursively();
	}

	// Installing over a plugin takes a ROM out of the bundle and puts it in the folder the plugin
	// reads. Carrying it into the new bundle would keep it, but keep it somewhere the next update
	// endangers all over again - so the update is the moment it gets moved for good.
	void testAnUpdateMovesTheRomOutOfTheBundle()
	{
		const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("tus_pm_test_migrate_roms");

		root.deleteRecursively();
		TEST_ASSERT(root.createDirectory().wasOk());

		const auto data = root.getChildFile("data");
		setDataFolder(data.getFullPathName().toStdString());

		const auto target = root.getChildFile("plugins");

		std::string error;
		std::vector<std::string> files;

		const auto build = root.getChildFile("build");

		TEST_ASSERT(build.getChildFile("Thing.vst3/Contents/MacOS").createDirectory().wasOk());
		TEST_ASSERT(build.getChildFile("Thing.vst3/Contents/MacOS/Thing").replaceWithText("v1"));

		const auto zip = root.getChildFile("build.zip");
		makeZip(build, zip);

		Asset asset;
		asset.product = "Thing";
		asset.format = "VST3";
		asset.version = "2.2.26";
		// file:/// because the installer downloads what it installs, and HttpClient takes a local
		// one of those - no server needed to exercise the whole path. Spelled out rather than via
		// juce::URL, which percent-encodes the drive colon into something localFile does not decode.
		asset.url = "file:///" + zip.getFullPathName().replaceCharacter('\\', '/').toStdString();

		const auto install = [&]
		{
			return ZipExtractor::install(zip.getFullPathName().toStdString(),
				target.getFullPathName().toStdString(), "Thing.vst3", "Thing", false, files, error);
		};

		TEST_ASSERT(install());

		// The ROM the user put next to the binary, inside the bundle.
		const auto inBundle = target.getChildFile("Thing.vst3/Contents/MacOS/thing_firmware.bin");

		TEST_ASSERT(inBundle.replaceWithText("irreplaceable"));

		// Updating moves it out. PluginInstaller rather than ZipExtractor, because the product name
		// is what says which roms folder, and only the installer knows it.
		PluginInstaller installer;

		TEST_ASSERT(installer.install(asset, {}, target.getFullPathName().toStdString(), {}, error));

		const auto kept = data.getChildFile("The Usual Suspects/Thing/roms/thing_firmware.bin");

		TEST_ASSERT(kept.existsAsFile());
		TEST_ASSERT(kept.loadFileAsString() == "irreplaceable");

		// And it is out of the bundle, not merely also in the folder - that is the whole point.
		TEST_ASSERT(!inBundle.exists());

		// The plugin still arrived.
		TEST_ASSERT(target.getChildFile("Thing.vst3/Contents/MacOS/Thing").existsAsFile());

		// Running again adds nothing: an install folder needing rights can leave the original in
		// place, and a roms folder collecting a copy per update is a plugin picking at random.
		TEST_ASSERT(inBundle.replaceWithText("irreplaceable"));
		TEST_ASSERT(installer.install(asset, {}, target.getFullPathName().toStdString(), {}, error));

		const auto roms = data.getChildFile("The Usual Suspects/Thing/roms");

		TEST_ASSERT(roms.findChildFiles(juce::File::findFiles, false).size() == 1);

		setDataFolder({});
		root.deleteRecursively();
	}

	// And removing a plugin moves that ROM out rather than taking it with the bundle. There is no
	// new build to carry it into here, and nothing to tell a user's file from the plugin's own, so
	// the one kind that cannot be downloaded again goes to the folder it should have been in.
	void testUninstallKeepsTheRomItFinds()
	{
		const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("tus_pm_test_uninstall_roms");

		root.deleteRecursively();
		TEST_ASSERT(root.createDirectory().wasOk());

		const auto data = root.getChildFile("data");
		setDataFolder(data.getFullPathName().toStdString());

		const auto target = root.getChildFile("plugins");

		std::string error;
		std::vector<std::string> files;

		const auto build = root.getChildFile("build");

		TEST_ASSERT(build.getChildFile("Thing.vst3/Contents/MacOS").createDirectory().wasOk());
		TEST_ASSERT(build.getChildFile("Thing.vst3/Contents/MacOS/Thing").replaceWithText("plugin"));

		const auto zip = root.getChildFile("build.zip");
		makeZip(build, zip);

		TEST_ASSERT(ZipExtractor::install(zip.getFullPathName().toStdString(),
			target.getFullPathName().toStdString(), "Thing.vst3", "Thing", false, files, error));

		// The ROM the user put next to the binary, and something of ours that must not be moved.
		const auto rom = target.getChildFile("Thing.vst3/Contents/MacOS/thing_firmware.bin");

		TEST_ASSERT(rom.replaceWithText("irreplaceable"));

		TEST_ASSERT(PluginInstaller::uninstall("Thing", "Thing", PluginFormat::VST3,
			target.getFullPathName().toStdString(), {}, error));

		// The plugin is gone...
		TEST_ASSERT(!target.getChildFile("Thing.vst3").exists());

		// ...and the ROM is in the folder the plugin reads, under its own name.
		const auto kept = data.getChildFile("The Usual Suspects/Thing/roms/thing_firmware.bin");

		TEST_ASSERT(kept.existsAsFile());
		TEST_ASSERT(kept.loadFileAsString() == "irreplaceable");

		setDataFolder({});
		root.deleteRecursively();
	}

	// A ROM inside the bundle survives the update that replaces the bundle. RomLoader always
	// searches the folder the binary sits in, which on macOS is inside the bundle, so that is a
	// place people really do keep ROMs - and an update used to delete the lot. The one product
	// that had not been updated was the only one that still had its ROM.
	// A ROM the user kept inside a plugin survives an update - and ends up somewhere the update
	// cannot break.
	//
	// It used to be carried into the new bundle, which keeps the file and breaks the plugin: a macOS
	// bundle is sealed, one file the signature does not cover is enough for the system to refuse it,
	// and the host then stops listing the plugin (BUG-10469). So it goes to the product's roms
	// folder, which is the other place the ROM loader looks, and the bundle is left exactly as it
	// was built.
	void testAnUpdateKeepsWhatTheUserPutInTheBundle()
	{
		const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("tus_pm_test_bundle_roms");

		root.deleteRecursively();
		TEST_ASSERT(root.createDirectory().wasOk());

		setDataFolder(root.getChildFile("data").getFullPathName().toStdString());

		const auto target = root.getChildFile("plugins");

		std::string error;
		std::vector<std::string> files;

		// The build that is already installed.
		const auto first = root.getChildFile("first");

		TEST_ASSERT(first.getChildFile("Thing.vst3/Contents/MacOS").createDirectory().wasOk());
		TEST_ASSERT(first.getChildFile("Thing.vst3/Contents/MacOS/Thing").replaceWithText("v1"));
		TEST_ASSERT(first.getChildFile("Thing.vst3/Contents/_CodeSignature").createDirectory().wasOk());
		TEST_ASSERT(first.getChildFile("Thing.vst3/Contents/_CodeSignature/CodeResources")
			.replaceWithText("signature of v1"));

		const auto firstZip = root.getChildFile("first.zip");
		makeZip(first, firstZip);

		TEST_ASSERT(ZipExtractor::install(firstZip.getFullPathName().toStdString(),
			target.getFullPathName().toStdString(), "Thing.vst3", "Thing", false, files, error));

		// What the user then puts next to the binary, which is one of the two places the ROM loader
		// looks, and the Finder's leavings from having opened the bundle to do it.
		const auto rom = target.getChildFile("Thing.vst3/Contents/MacOS/thing_firmware.bin");

		TEST_ASSERT(rom.replaceWithText("a ROM nobody can download again"));
		TEST_ASSERT(target.getChildFile("Thing.vst3/Contents/MacOS/.DS_Store").replaceWithText("finder"));

		// And the next build, which knows nothing about it.
		const auto second = root.getChildFile("second");

		TEST_ASSERT(second.getChildFile("Thing.vst3/Contents/MacOS").createDirectory().wasOk());
		TEST_ASSERT(second.getChildFile("Thing.vst3/Contents/MacOS/Thing").replaceWithText("v2"));

		const auto secondZip = root.getChildFile("second.zip");
		makeZip(second, secondZip);

		TEST_ASSERT(ZipExtractor::install(secondZip.getFullPathName().toStdString(),
			target.getFullPathName().toStdString(), "Thing.vst3", "Thing", false, files, error));

		// The update happened...
		TEST_ASSERT(target.getChildFile("Thing.vst3/Contents/MacOS/Thing").loadFileAsString() == "v2");

		// ...and the ROM is kept, in the folder the plugin reads ROMs from.
		const auto kept = juce::File(juce::String(PlatformUtils::romFolder("Thing")))
			.getChildFile("thing_firmware.bin");

		TEST_ASSERT(kept.existsAsFile());
		TEST_ASSERT(kept.loadFileAsString() == "a ROM nobody can download again");

		// Nothing that was not built into it is inside the new bundle: not the ROM, not the signature
		// of the build that is gone, not the file the Finder left. One file that the signature does
		// not cover is all it takes for macOS to refuse the whole plugin.
		TEST_ASSERT(!rom.exists());
		TEST_ASSERT(!target.getChildFile("Thing.vst3/Contents/_CodeSignature/CodeResources").exists());
		TEST_ASSERT(!target.getChildFile("Thing.vst3/Contents/MacOS/.DS_Store").exists());
		TEST_ASSERT(target.getChildFile("Thing.vst3").findChildFiles(juce::File::findFiles, true).size() == 1);

		// Nothing of the replaced copy is left lying beside it either.
		TEST_ASSERT(target.findChildFiles(juce::File::findDirectories, false).size() == 1);

		// A third update keeps one copy rather than a numbered pile of them: the ROM is in the roms
		// folder now, so the install finds nothing of the user's left in the bundle to carry out.
		TEST_ASSERT(ZipExtractor::install(secondZip.getFullPathName().toStdString(),
			target.getFullPathName().toStdString(), "Thing.vst3", "Thing", false, files, error));

		TEST_ASSERT(juce::File(juce::String(PlatformUtils::romFolder("Thing")))
			.findChildFiles(juce::File::findFiles, false).size() == 1);

		setDataFolder({});
		root.deleteRecursively();
	}

	// What the header button offers, and what it must not offer: a build older than the one
	// running is what somebody rolling back asks for by hand, and the same one is no update at
	// all. Written against versions nothing will ever reach rather than against a number near the
	// current one, so the test does not expire with the next release.
	void testAnUpdateIsOnlyOfferedForANewerBuild()
	{
		const auto os = PlatformUtils::getCurrentOS();
		const auto arch = PlatformUtils::getCurrentArch();

		const auto assetFor = [&](const std::string& _version)
		{
			Asset a;
			a.product = g_appProduct;
			a.format = "Standalone";
			a.version = _version;
			a.os = os;
			a.arch = arch;
			a.package = "zip";
			a.url = "https://example.invalid/" + _version;
			return a;
		};

		// Nothing of this program in the catalog at all, which is the usual answer: its builds sit
		// behind the team login.
		{
			Catalog c;
			TEST_ASSERT(SelfUpdate::find(c) == nullptr);
		}

		// Older, and the same one that is running.
		{
			Catalog c;
			c.addAssets({assetFor("0.0.1"), assetFor(g_appVersion)});
			TEST_ASSERT(SelfUpdate::find(c) == nullptr);
		}

		// Newer, among older ones: the newest is what is offered.
		{
			Catalog c;
			c.addAssets({assetFor("0.0.1"), assetFor("999.0.1"), assetFor("999.0.0")});

			const auto* found = SelfUpdate::find(c);

			TEST_ASSERT(found && found->version == "999.0.1");
		}

		// A build for another machine is not an update for this one.
		{
			Catalog c;
			auto other = assetFor("999.0.1");
			other.os = os == "windows" ? "linux" : "windows";
			other.arch = "x86_64";
			c.addAssets({other});

			TEST_ASSERT(SelfUpdate::find(c) == nullptr);
		}

		// The copy an update starts says so, and nothing else does - that flag is what lets it past
		// the one-at-a-time check while the copy that started it is still quitting.
		TEST_ASSERT(SelfUpdate::isRestart({"--mcp", "--updated"}));
		TEST_ASSERT(!SelfUpdate::isRestart({"--mcp"}));
		TEST_ASSERT(!SelfUpdate::isRestart({}));
	}

	// A build packed without its program: the archive holds the changelog every package gets and nothing
	// else. That changelog is not installed, so nothing would be, and the install used to say it worked
	// while the row never turned installed.
	void testAnArchiveWithNothingToInstallFails()
	{
		const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("pluginManagerTest-empty-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

		const auto content = root.getChildFile("content");
		const auto target = root.getChildFile("programs");

		TEST_ASSERT(content.createDirectory().wasOk());
		TEST_ASSERT(target.createDirectory().wasOk());
		TEST_ASSERT(content.getChildFile("changelog_Synth-1000.txt").replaceWithText("notes"));

		const auto zip = root.getChildFile("build.zip");

		makeZip(content, zip);

		std::vector<std::string> files;
		std::string error;

		TEST_ASSERT(!ZipExtractor::install(zip.getFullPathName().toStdString(), target.getFullPathName().toStdString(),
			"Synth-1000.exe", "Synth-1000", false, files, error));

		TEST_ASSERT(!error.empty());
		TEST_ASSERT(files.empty());
		TEST_ASSERT(target.findChildFiles(juce::File::findFilesAndDirectories, false).isEmpty());

		root.deleteRecursively();
	}

	void testInstallFlattensAnArchivePrefix()
	{
		const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("pluginManagerTest-layout-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

		const auto content = root.getChildFile("content");
		const auto target = root.getChildFile("plugins");

		// What a Linux zip holds: a whole install prefix, with the plugin several folders down and a
		// changelog somewhere else in it.
		const auto plugin = content.getChildFile("usr/local/lib/vst3/Synth-1000.vst3");

		TEST_ASSERT(plugin.getChildFile("Contents").createDirectory().wasOk());
		TEST_ASSERT(plugin.getChildFile("Contents/binary").replaceWithText("x"));
		TEST_ASSERT(content.getChildFile("usr/local/changelog.txt").replaceWithText("notes"));

		const auto zip = root.getChildFile("build.zip");

		makeZip(content, zip);

		std::vector<std::string> files;
		std::string error;

		TEST_ASSERT(ZipExtractor::install(zip.getFullPathName().toStdString(), target.getFullPathName().toStdString(),
			"Synth-1000.vst3", "Synth-1000", false, files, error));

		// Where a host looks, and nothing of the prefix around it.
		TEST_ASSERT(target.getChildFile("Synth-1000.vst3/Contents/binary").existsAsFile());
		TEST_ASSERT(!target.getChildFile("usr").exists());

		// An archive that holds the plugin at the top - which is every Windows and macOS one - is
		// installed as it comes.
		const auto flat = root.getChildFile("flat");
		const auto flatTarget = root.getChildFile("plugins2");

		TEST_ASSERT(flat.getChildFile("Synth-1000.vst3/Contents").createDirectory().wasOk());
		TEST_ASSERT(flat.getChildFile("Synth-1000.vst3/Contents/binary").replaceWithText("y"));
		TEST_ASSERT(flat.getChildFile("changelog.txt").replaceWithText("notes"));
		TEST_ASSERT(flat.getChildFile("Synth-1000.vst3/Contents/notes.txt").replaceWithText("inside"));

		const auto flatZip = root.getChildFile("flat.zip");

		makeZip(flat, flatZip);

		// BUG-10462: one an earlier version of this program left there. It goes with this install.
		TEST_ASSERT(flatTarget.createDirectory().wasOk());
		TEST_ASSERT(flatTarget.getChildFile("changelog.txt").replaceWithText("stale"));

		TEST_ASSERT(ZipExtractor::install(flatZip.getFullPathName().toStdString(),
			flatTarget.getFullPathName().toStdString(), "Synth-1000.vst3", "Synth-1000", false, files, error));

		TEST_ASSERT(flatTarget.getChildFile("Synth-1000.vst3/Contents/binary").existsAsFile());

		// The changelog in the archive is for whoever downloaded the archive. A plugin folder is
		// read by hosts, and every build used to leave one of these in it.
		TEST_ASSERT(!flatTarget.getChildFile("changelog.txt").exists());

		// Nor is it claimed as something this install wrote, which is what an uninstall goes by.
		TEST_ASSERT(std::find(files.begin(), files.end(), "changelog.txt") == files.end());
		TEST_ASSERT(std::find(files.begin(), files.end(), "Synth-1000.vst3") != files.end());

		// A .txt inside the plugin is the plugin's own and is left alone.
		TEST_ASSERT(flatTarget.getChildFile("Synth-1000.vst3/Contents/notes.txt").existsAsFile());

		// Nothing of the download is left in the temp folder either.
		TEST_ASSERT(juce::File::getSpecialLocation(juce::File::tempDirectory)
			.findChildFiles(juce::File::findDirectories, false, "tus-install-*").isEmpty());

		root.deleteRecursively();
	}

	// An install that cannot finish must not have started. The usual reason it cannot is a plugin
	// a host has open, and what that leaves behind if the copy is allowed to begin is a bundle made
	// of two versions - a port description in front of a binary that does not match it, which is
	// not something any host can load.
	void testInstallLeavesTheOldBuildAloneWhenItCannotFinish()
	{
		const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("pluginManagerTest-replace-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

		const auto target = root.getChildFile("plugins");
		const auto bundle = target.getChildFile("Synth-1000.vst3");

		// What is installed already: a bundle of two files, and a third that the same archive
		// brought along last time.
		TEST_ASSERT(bundle.getChildFile("Contents").createDirectory().wasOk());
		TEST_ASSERT(bundle.getChildFile("Contents/binary").replaceWithText("old"));
		TEST_ASSERT(bundle.getChildFile("Contents/ports").replaceWithText("old"));
		TEST_ASSERT(target.getChildFile("changelog.txt").replaceWithText("old"));

		// And the build that is meant to replace it.
		const auto content = root.getChildFile("content");

		TEST_ASSERT(content.getChildFile("Synth-1000.vst3/Contents").createDirectory().wasOk());
		TEST_ASSERT(content.getChildFile("Synth-1000.vst3/Contents/binary").replaceWithText("new"));
		TEST_ASSERT(content.getChildFile("Synth-1000.vst3/Contents/ports").replaceWithText("new"));
		TEST_ASSERT(content.getChildFile("changelog.txt").replaceWithText("new"));

		const auto zip = root.getChildFile("build.zip");

		makeZip(content, zip);

		// One file of what is installed cannot be written to, which is what a host holding it open
		// looks like from here.
		const auto held = bundle.getChildFile("Contents/binary");

		TEST_ASSERT(held.setReadOnly(true));

		// A process running as root writes to it anyway, and then there is nothing here to see.
		if (juce::FileOutputStream(held).openedOk())
		{
			held.setReadOnly(false);
			root.deleteRecursively();
			return;
		}

		std::vector<std::string> files;
		std::string error;

		TEST_ASSERT(!ZipExtractor::install(zip.getFullPathName().toStdString(),
			target.getFullPathName().toStdString(), "Synth-1000.vst3", "Synth-1000", false, files, error));

		// The folder is as it was. Not one file of the new build went in, including the ones
		// that could have been written.
		TEST_ASSERT(bundle.getChildFile("Contents/binary").loadFileAsString() == "old");
		TEST_ASSERT(bundle.getChildFile("Contents/ports").loadFileAsString() == "old");
		TEST_ASSERT(target.getChildFile("changelog.txt").loadFileAsString() == "old");
		TEST_ASSERT(target.findChildFiles(juce::File::findFilesAndDirectories, false, "*.tus-replaced*").isEmpty());

		// And it says which file, because "could not install" is not something a user can act on.
		TEST_ASSERT(error.find(held.getFullPathName().toStdString()) != std::string::npos);

		// Once it can be written to, the same install goes through and all of it is the new build.
		TEST_ASSERT(held.setReadOnly(false));

		TEST_ASSERT(ZipExtractor::install(zip.getFullPathName().toStdString(),
			target.getFullPathName().toStdString(), "Synth-1000.vst3", "Synth-1000", false, files, error));

		TEST_ASSERT(bundle.getChildFile("Contents/binary").loadFileAsString() == "new");
		TEST_ASSERT(bundle.getChildFile("Contents/ports").loadFileAsString() == "new");

		// And the changelog the earlier version of this program left here goes with it (BUG-10462),
		// which could only happen once the copy itself had been made.
		TEST_ASSERT(!target.getChildFile("changelog.txt").exists());

		// Nothing of the swap is left behind either.
		TEST_ASSERT(target.findChildFiles(juce::File::findFilesAndDirectories, false, "*.tus-replaced*").isEmpty());

		root.deleteRecursively();
	}

	// An archive brings more than the plugin - a changelog beside it, and for the standalone builds
	// a second program, a readme and a licence. An uninstall that only knows the plugin's own name
	// leaves all of that behind for good, so what an install wrote is written down.
	//
	// And two installs can bring the same file into the same folder: every effect build carries the
	// changelog of the plugin it belongs to. Taking one away must not take the other's copy.
	// A program keeps the name it was built under: OsirusTestConsole ships virusTestConsole.exe, so
	// nothing named after the product is ever on the disk. Everything that asks whether it is
	// installed used to look for that name and find nothing, which left a console that had
	// installed perfectly well showing as missing, offering Install for ever, and refusing to be
	// removed. What the install wrote is the answer, and only the manifest knows it.
	void testAProgramKeepsTheNameItWasBuiltUnder()
	{
		const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("pluginManagerTest-programs-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

		const auto programs = dir.getChildFile("programs");

		TEST_ASSERT(programs.createDirectory().wasOk());

		PlatformUtils::setSettingsFolder(dir.getFullPathName().toStdString());

		Asset console;
		console.product = "OsirusTestConsole";
		console.format = "Standalone";
		console.version = "2.2.25";

		// What that archive actually holds. The product name is in none of it - which is also why
		// each program is installed into a folder named after it: the OsTIrus console carries a
		// virusTestConsole and the same .bat, and one folder for both would lose one of them.
		const std::vector<std::string> files{"virusTestConsole.exe", "start_IndiArp_BC.bat"};

		const auto installed = programs.getChildFile("OsirusTestConsole");

		{
			PluginManager pm(false);

			pm.getInstallPaths().set(PluginFormat::Standalone, programs.getFullPathName().toStdString());

			// Written down, because the manager opened below has to find the same folder.
			pm.getInstallPaths().save();

			// A folder of its own under the location for the format, and that is where everything else
			// here looks for it too.
			TEST_ASSERT(pm.getInstallPath("OsirusTestConsole", "Standalone") ==
				installed.getFullPathName().toStdString());

			TEST_ASSERT(installed.createDirectory().wasOk());

			for (const auto& file : files)
				TEST_ASSERT(installed.getChildFile(juce::String(file)).replaceWithText("program"));

			pm.recordInstall(console, files);

			// The two answers a row is built from, neither of which can come from the product name.
			TEST_ASSERT(pm.isInstalled("OsirusTestConsole", "Standalone"));
			TEST_ASSERT(pm.getInstalledVersion("OsirusTestConsole", "Standalone") == "2.2.25");

			// And they survive being written down and read back, which is the part a program used to
			// fail: the record was dropped on the way out because the file it named was not there, so
			// an install was forgotten the moment it finished.
			{
				const PluginManager reopened;

				TEST_ASSERT(reopened.isInstalled("OsirusTestConsole", "Standalone"));
				TEST_ASSERT(reopened.getInstalledVersion("OsirusTestConsole", "Standalone") == "2.2.25");
			}

			std::string message;

			TEST_ASSERT(pm.uninstall("OsirusTestConsole", "Standalone", message));

			for (const auto& file : files)
				TEST_ASSERT(!installed.getChildFile(juce::String(file)).exists());

			TEST_ASSERT(!pm.isInstalled("OsirusTestConsole", "Standalone"));
		}

		// A record whose files somebody took away by hand is not an install either, or the row would
		// offer to remove what is not there and report a version nothing on the disk has.
		{
			PluginManager pm(false);

			pm.getInstallPaths().set(PluginFormat::Standalone, programs.getFullPathName().toStdString());

			TEST_ASSERT(installed.createDirectory().wasOk());
			TEST_ASSERT(installed.getChildFile("virusTestConsole.exe").replaceWithText("program"));

			pm.recordInstall(console, files);

			TEST_ASSERT(pm.isInstalled("OsirusTestConsole", "Standalone"));
			TEST_ASSERT(installed.getChildFile("virusTestConsole.exe").deleteFile());

			TEST_ASSERT(!pm.isInstalled("OsirusTestConsole", "Standalone"));
			TEST_ASSERT(pm.getInstalledVersion("OsirusTestConsole", "Standalone").empty());
		}

		PlatformUtils::setSettingsFolder({});
		dir.deleteRecursively();
	}

	// The program of an effect build is installed beside the plugin's own when that is installed, so
	// that folder is where it has to be looked for too. It was looked for only in a folder named after
	// itself, and every effect standalone installed next to its plugin read as not installed, offering
	// Install again after every install (BUG-10493).
	void testAnEffectProgramIsFoundBesideItsPlugin()
	{
		const Fixture fixture;

		const auto programs = fixture.dir.getChildFile("programs");
		const auto pluginFolder = programs.getChildFile("Synth-1000");
		TEST_ASSERT(pluginFolder.createDirectory().wasOk());

		PlatformUtils::setSettingsFolder(fixture.dir.getChildFile("settings").getFullPathName().toStdString());

		// What an install into the default location leaves, from the reporter's installed.json: the
		// effect build's program in the plugin's folder, not in one named after itself. The default
		// location is the user's own Applications folder, which a test does not write to, so the state
		// is set up rather than installed.
		{
			InstalledPlugins records;

			for (const std::string product : {"Synth-1000", "Synth-1000FX"})
			{
				Asset program;
				program.product = product;
				program.format = "Standalone";
				program.version = "2.2.19";

				const auto name = product + ".app";
				TEST_ASSERT(pluginFolder.getChildFile(juce::String(name)).replaceWithText("program"));

				records.set(program, pluginFolder.getFullPathName().toStdString(), {name});
			}

			records.save();
		}

		{
			// Reading the settings - the test's own folder - is what brings in the records above.
			PluginManager pm(true);
			pm.setCredentials(Channel::Internal, {}, {"team", "secret"});

			std::string error;
			TEST_ASSERT(pm.refresh(error));

			pm.getInstallPaths().set(PluginFormat::Standalone, programs.getFullPathName().toStdString());

			TEST_ASSERT(pm.isInstalled("Synth-1000", "Standalone"));
			TEST_ASSERT(pm.isInstalled("Synth-1000FX", "Standalone"));
			TEST_ASSERT(pm.getInstalledVersion("Synth-1000FX", "Standalone") == "2.2.19");
		}

		PlatformUtils::setSettingsFolder({});
	}

	// The case behind most of the questions about effect builds (BUG-10488): the plugin is up to date and
	// its effect build is not, with the FX chip off, which is how it starts. "Updates available" counts
	// the effect build, so the column has to offer the upgrade that takes it along - it offered a
	// reinstall, the row never left that group, and the effect build was never upgraded.
	void testAnInstalledEffectBuildKeepsUpWithItsPlugin()
	{
		const Fixture fixture;

		// The builds of this platform, which is the one every answer below is about.
		const std::string platform =
#if JUCE_WINDOWS
			"win64";
#elif JUCE_MAC
			"MacOS";
#elif defined(__aarch64__)
			"Linux_aarch64";
#else
			"Linux_x86_64";
#endif

		const auto link = [&](const std::string& _product)
		{
			const auto name = "TheUsualSuspects-" + _product + "-VST3-2.2.19-" + platform + ".zip";
			return "<a href=\"/builds/prototype-9/internal/" + name + "\">x</a>";
		};

		fixture.write("builds/prototype-9/internal/index.html", link("Prototype-9") + link("Prototype-9FX"));

		const auto plugins = fixture.dir.getChildFile("vst3");
		TEST_ASSERT(plugins.createDirectory().wasOk());

		PlatformUtils::setSettingsFolder(fixture.dir.getChildFile("settings").getFullPathName().toStdString());

		const auto installed = [&](const std::string& _product, const std::string& _version)
		{
			Asset build;
			build.product = _product;
			build.format = "VST3";
			build.version = _version;
			return build;
		};

		// The plugin at the newest version, its effect build one behind.
		{
			InstalledPlugins records;

			for (const auto& [product, version] : {std::make_pair(std::string("Prototype-9"), std::string("2.2.19")),
			                                       std::make_pair(std::string("Prototype-9FX"), std::string("2.2.18"))})
			{
				TEST_ASSERT(plugins.getChildFile(juce::String(product + ".vst3")).replaceWithText("plugin"));
				records.set(installed(product, version), plugins.getFullPathName().toStdString(), {product + ".vst3"});
			}

			records.save();
		}

		{
			// Reading the settings - the test's own folder - is what brings in the records above.
			PluginManager pm(true);
			pm.setCredentials(Channel::Internal, {}, {"team", "secret"});

			std::string error;
			TEST_ASSERT(pm.refresh(error));

			pm.getInstallPaths().set(PluginFormat::VST3, plugins.getFullPathName().toStdString());

			TEST_ASSERT(pm.getCatalog().fxProductOf("Prototype-9") == "Prototype-9FX");
			TEST_ASSERT(pm.hasUpdate("Prototype-9"));

			// What the column is made of, with the chip off.
			const auto actionOf = [&](const std::string& _product)
			{
				return pm.installAction(_product, "VST3", true, pm.getInstalledVersion(_product, "VST3"), "2.2.19");
			};

			const auto plugin = actionOf("Prototype-9");
			const auto fx = actionOf("Prototype-9FX");

			TEST_ASSERT(plugin == InstallAction::Reinstall);
			TEST_ASSERT(fx == InstallAction::Upgrade);

			// Installed, so it is part of the column although nobody ticked it, and the column offers
			// what the group it sits in says: an upgrade.
			const auto fxInstalled = pm.isInstalled("Prototype-9FX", "VST3");
			TEST_ASSERT(fxInstalled);
			TEST_ASSERT(includesFxBuild(true, false, fxInstalled));
			TEST_ASSERT(combineInstallActions(plugin, fx) == InstallAction::Upgrade);

			// And that upgrade fetches the effect build alone, not the plugin that is current (BUG-10486).
			TEST_ASSERT(!fetchesPart(plugin, combineInstallActions(plugin, fx)));
			TEST_ASSERT(fetchesPart(fx, combineInstallActions(plugin, fx)));

			// And once that upgrade has run, the row has nothing left to offer.
			pm.recordInstall(installed("Prototype-9FX", "2.2.19"), {"Prototype-9FX.vst3"});

			TEST_ASSERT(!pm.hasUpdate("Prototype-9"));
			TEST_ASSERT(actionOf("Prototype-9FX") == InstallAction::Reinstall);
		}

		PlatformUtils::setSettingsFolder({});
	}

	void testUninstallTakesWhatTheArchiveBrought()
	{
		const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("pluginManagerTest-leftovers-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

		const auto plugins = dir.getChildFile("plugins");

		TEST_ASSERT(plugins.createDirectory().wasOk());

		PlatformUtils::setSettingsFolder(dir.getFullPathName().toStdString());

		{
			PluginManager pm(false);

			pm.getInstallPaths().set(PluginFormat::VST3, plugins.getFullPathName().toStdString());

			// What two installs into one folder look like afterwards: a plugin each, and one changelog
			// that both of them brought.
			TEST_ASSERT(plugins.getChildFile("Synth-1000.vst3").createDirectory().wasOk());
			TEST_ASSERT(plugins.getChildFile("Synth-1000FX.vst3").createDirectory().wasOk());
			TEST_ASSERT(plugins.getChildFile("changelog_Synth-1000.txt").replaceWithText("notes"));
			TEST_ASSERT(plugins.getChildFile("SomethingElse.vst3").replaceWithText("not ours"));

			Asset plugin;
			plugin.product = "Synth-1000";
			plugin.format = "VST3";
			plugin.version = "2.2.19";

			auto fx = plugin;
			fx.product = "Synth-1000FX";

			pm.recordInstall(plugin, {"Synth-1000.vst3", "changelog_Synth-1000.txt"});
			pm.recordInstall(fx, {"Synth-1000FX.vst3", "changelog_Synth-1000.txt"});

			std::string message;

			// The plugin goes. The changelog stays, because the effect build still wants it - and what
			// was in the folder already is nobody's business.
			TEST_ASSERT(pm.uninstall("Synth-1000", "VST3", message));

			TEST_ASSERT(!plugins.getChildFile("Synth-1000.vst3").exists());
			TEST_ASSERT(plugins.getChildFile("Synth-1000FX.vst3").exists());
			TEST_ASSERT(plugins.getChildFile("changelog_Synth-1000.txt").exists());
			TEST_ASSERT(plugins.getChildFile("SomethingElse.vst3").exists());

			// Now nothing else claims it, so it goes with the last one out.
			TEST_ASSERT(pm.uninstall("Synth-1000FX", "VST3", message));

			TEST_ASSERT(!plugins.getChildFile("Synth-1000FX.vst3").exists());
			TEST_ASSERT(!plugins.getChildFile("changelog_Synth-1000.txt").exists());
			TEST_ASSERT(plugins.getChildFile("SomethingElse.vst3").exists());
		}

		// An install from before any of this was written down has no list, and then the plugin is
		// still all that can be found to remove - rather than nothing at all.
		{
			PluginManager pm(false);

			pm.getInstallPaths().set(PluginFormat::VST3, plugins.getFullPathName().toStdString());

			TEST_ASSERT(plugins.getChildFile("Synth-2000.vst3").createDirectory().wasOk());
			TEST_ASSERT(plugins.getChildFile("changelog_Synth-2000.txt").replaceWithText("notes"));

			Asset old;
			old.product = "Synth-2000";
			old.format = "VST3";
			old.version = "2.2.16";

			pm.recordInstall(old);

			std::string message;

			TEST_ASSERT(pm.uninstall("Synth-2000", "VST3", message));
			TEST_ASSERT(!plugins.getChildFile("Synth-2000.vst3").exists());
			TEST_ASSERT(plugins.getChildFile("changelog_Synth-2000.txt").exists());
		}

		PlatformUtils::setSettingsFolder({});
		dir.deleteRecursively();
	}

	// Pointing a format somewhere else does not move what is already installed, and the record of
	// it stops matching, so from then on nothing in the program mentions that copy again - while
	// every host that scans the old folder still loads it.
	void testAnInstallLeftBehindIsStillFound()
	{
		const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("pluginManagerTest-stray-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

		const auto oldDir = dir.getChildFile("old");
		const auto newDir = dir.getChildFile("new");

		TEST_ASSERT(oldDir.createDirectory().wasOk());
		TEST_ASSERT(newDir.createDirectory().wasOk());

		PlatformUtils::setSettingsFolder(dir.getFullPathName().toStdString());

		{
			PluginManager pm(false);

			pm.getInstallPaths().set(PluginFormat::VST3, oldDir.getFullPathName().toStdString());

			TEST_ASSERT(oldDir.getChildFile("Synth-1000.vst3").createDirectory().wasOk());
			TEST_ASSERT(oldDir.getChildFile("changelog_Synth-1000.txt").replaceWithText("notes"));

			Asset asset;
			asset.product = "Synth-1000";
			asset.format = "VST3";
			asset.version = "2.2.19";

			pm.recordInstall(asset, {"Synth-1000.vst3", "changelog_Synth-1000.txt"});

			// Where it was installed is where it goes, so nothing was left anywhere.
			TEST_ASSERT(pm.getStrayInstalls().empty());
			TEST_ASSERT(pm.getInstalledVersion("Synth-1000", "VST3") == "2.2.19");

			pm.getInstallPaths().set(PluginFormat::VST3, newDir.getFullPathName().toStdString());

			// Now it is neither installed where it goes nor gone from where it was.
			TEST_ASSERT(pm.getInstalledVersion("Synth-1000", "VST3").empty());

			const auto strays = pm.getStrayInstalls();

			TEST_ASSERT(strays.size() == 1);
			TEST_ASSERT(strays[0].product == "Synth-1000");
			TEST_ASSERT(strays[0].version == "2.2.19");
			TEST_ASSERT(juce::File(juce::String(strays[0].path)).getParentDirectory() == oldDir);

			// Removing one takes it from where it is rather than from where the format now points,
			// and takes what the same archive left beside it.
			std::string message;

			TEST_ASSERT(pm.removeStray(strays[0], message));

			TEST_ASSERT(!oldDir.getChildFile("Synth-1000.vst3").exists());
			TEST_ASSERT(!oldDir.getChildFile("changelog_Synth-1000.txt").exists());
			TEST_ASSERT(pm.getStrayInstalls().empty());
		}

		// A record pointing at another folder says nothing about this one: the names in it belong
		// there, and an uninstall here must not go by them. Without this, removing the build that
		// somebody put in the new location by hand would delete the old location's extras with it.
		{
			PluginManager pm(false);

			pm.getInstallPaths().set(PluginFormat::VST3, oldDir.getFullPathName().toStdString());

			TEST_ASSERT(oldDir.getChildFile("Synth-2000.vst3").createDirectory().wasOk());
			TEST_ASSERT(oldDir.getChildFile("changelog_Synth-2000.txt").replaceWithText("notes"));

			Asset asset;
			asset.product = "Synth-2000";
			asset.format = "VST3";
			asset.version = "2.2.19";

			pm.recordInstall(asset, {"Synth-2000.vst3", "changelog_Synth-2000.txt"});

			// The location moves, and a build turns up there by other means.
			pm.getInstallPaths().set(PluginFormat::VST3, newDir.getFullPathName().toStdString());

			TEST_ASSERT(newDir.getChildFile("Synth-2000.vst3").createDirectory().wasOk());

			std::string message;

			TEST_ASSERT(pm.uninstall("Synth-2000", "VST3", message));

			TEST_ASSERT(!newDir.getChildFile("Synth-2000.vst3").exists());
			TEST_ASSERT(oldDir.getChildFile("Synth-2000.vst3").exists());
			TEST_ASSERT(oldDir.getChildFile("changelog_Synth-2000.txt").exists());
		}

		PlatformUtils::setSettingsFolder({});
		dir.deleteRecursively();
	}

	// A donator folder may be typed as its name or pasted as the link from the download post, and
	// both mean the same folder (EMU-219). The link used to be put between the base and the tier a
	// second time: .../builds/https://dsp56300.com/builds/synth-1000/donators/donators/.
	void testADonatorFolderMayBeTypedAsALink()
	{
		TEST_ASSERT(CatalogSource::folderName("synth-1000") == "synth-1000");
		TEST_ASSERT(CatalogSource::folderName("https://dsp56300.com/builds/synth-1000/donators/") == "synth-1000");
		TEST_ASSERT(CatalogSource::folderName("https://dsp56300.com/builds/synth-1000/donators") == "synth-1000");
		TEST_ASSERT(CatalogSource::folderName("https://dsp56300.com/builds/synth-1000/") == "synth-1000");
		TEST_ASSERT(CatalogSource::folderName("dsp56300.com/builds/synth-1000/internal/") == "synth-1000");

		// The link from a post made after the move names the folder the same way.
		TEST_ASSERT(CatalogSource::folderName(urls().serverBuilds + "synth-1000/donators/") == "synth-1000");
		TEST_ASSERT(CatalogSource::folderName("  /Synth-1000/  ") == "synth-1000");
		TEST_ASSERT(CatalogSource::folderName("rack-a-100") == "rack-a-100");

		// The URL is built from the name, never from the link around it.
		TEST_ASSERT(CatalogSource::channelUrl("https://dsp56300.com/builds/synth-1000/donators/", Channel::Donators)
			== CatalogSource::channelUrl("synth-1000", Channel::Donators));

		// And a login stored the one way is found the other: the catalog asks with the name.
		CredentialStore store(false);
		store.set(Channel::Donators, "https://dsp56300.com/builds/synth-1000/donators/", {"synth-1000", "letmein"}, false);

		TEST_ASSERT(store.get(Channel::Donators, "synth-1000").password == "letmein");
		TEST_ASSERT(store.getSlugs(Channel::Donators).size() == 1);
		TEST_ASSERT(store.getSlugs(Channel::Donators)[0] == "synth-1000");
	}

	// The stored logins are not left lying in the open: not legible in the file, and not readable by
	// another account on the machine (BUG-10460).
	//
	// Scrambling is not encryption and is not pretending to be - the program reads the password back
	// with nobody to ask, so the key is in the program. It stops the password being read at a glance;
	// the file mode is what keeps another account out.
	void testStoredLoginsAreNotLyingInTheOpen()
	{
		const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("pluginManagerTest-creds-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

		TEST_ASSERT(dir.createDirectory().wasOk());

		PlatformUtils::setSettingsFolder(dir.getFullPathName().toStdString());

		const auto file = juce::File(juce::String(PlatformUtils::getSettingsPath("credentials.xml")));

		{
			CredentialStore store(false);

			store.set(Channel::Donators, "synth-1000", {"someone", "hunter2"});
			store.set(Channel::Internal, {}, {"team", "teampass"});
		}

		const auto written = file.loadFileAsString();

		TEST_ASSERT(written.isNotEmpty());

		// Neither password is in there to be read, and neither is a user name's worth of a hint.
		TEST_ASSERT(!written.contains("hunter2"));
		TEST_ASSERT(!written.contains("teampass"));

		// The user names are not secret and stay as they are - somebody opening the file should be
		// able to see which logins are stored.
		TEST_ASSERT(written.contains("someone"));

		{
			CredentialStore reopened;

			TEST_ASSERT(reopened.get(Channel::Donators, "synth-1000").password == "hunter2");
			TEST_ASSERT(reopened.get(Channel::Internal, {}).password == "teampass");
		}

		// A blob belongs to the entry it was written for: moved to another one it decodes to rubbish
		// rather than to a working password.
		TEST_ASSERT(LoginFile::reveal(LoginFile::hide("hunter2", "donators:synth-1000"), "internal") != "hunter2");

#ifndef _WIN32
		// 644 is what the usual umask gives, and that is every account on the machine. The mode is
		// set on every save, because a file an older version wrote keeps the mode it was given.
		struct stat st = {};
		TEST_ASSERT(stat(file.getFullPathName().toRawUTF8(), &st) == 0);
		TEST_ASSERT((st.st_mode & 0777) == 0600);
#endif

		// A file written by a version that kept the password as typed still works, and saving once
		// takes the plain copy out of it. Nobody has to log in again.
		TEST_ASSERT(file.replaceWithText(
			"<credentials>\n"
			"  <login key=\"donators:rack-a-100\" user=\"old\" password=\"plaintext1\"/>\n"
			"</credentials>\n"));

		{
			CredentialStore old;

			TEST_ASSERT(old.get(Channel::Donators, "rack-a-100").user == "old");
			TEST_ASSERT(old.get(Channel::Donators, "rack-a-100").password == "plaintext1");

			old.set(Channel::Donators, "rack-a-100", old.get(Channel::Donators, "rack-a-100"));
		}

		const auto rewritten = file.loadFileAsString();

		TEST_ASSERT(!rewritten.contains("plaintext1"));

		{
			CredentialStore reopened;
			TEST_ASSERT(reopened.get(Channel::Donators, "rack-a-100").password == "plaintext1");
		}

		PlatformUtils::setSettingsFolder({});
		dir.deleteRecursively();
	}

	// A build that is already installed is found wherever it is, not only where this would put it.
	//
	// People open this for the first time with every plugin we make already on their disk, and were
	// offered a download of each one: nothing was written down yet, and the only folder asked about
	// was the one an install would go to. A Mac keeps most plugins in the system /Library rather than
	// the user's own, which is where that folder points, so on a Mac that was every one of them.
	void testAnInstalledBuildIsFoundWhereverItIs()
	{
		const Fixture fixture;

		const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("pluginManagerTest-locate-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

		const auto elsewhere = dir.getChildFile("elsewhere");
		const auto chosen = dir.getChildFile("chosen");

		TEST_ASSERT(elsewhere.createDirectory().wasOk());
		TEST_ASSERT(chosen.createDirectory().wasOk());

		PlatformUtils::setSettingsFolder(dir.getFullPathName().toStdString());

		// Standing in for a standard location of this system, because a test must not install into
		// one of the real ones - and on the machine this runs on there may be a plugin in it.
		PlatformUtils::setSearchPaths(PluginFormat::VST3, {elsewhere.getFullPathName().toStdString()});

		// Unpacked by hand, before this program was ever started: no record of it anywhere.
		TEST_ASSERT(elsewhere.getChildFile("Synth-1000.vst3").createDirectory().wasOk());

		{
			auto pm = makeManager(true);

			std::string error;
			TEST_ASSERT(pm.refresh(error));

			TEST_ASSERT(pm.isInstalled("Synth-1000", "VST3"));
			TEST_ASSERT(pm.locateInstalled("Synth-1000", PluginFormat::VST3) ==
				elsewhere.getFullPathName().toStdString());

			// And that is where an upgrade of it goes. A second copy in the other folder would leave
			// the host loading one of two versions and nobody able to say which.
			TEST_ASSERT(pm.getInstallPath("Synth-1000", "VST3") == elsewhere.getFullPathName().toStdString());

			// An effect build follows the plugin it belongs to, which is the folder the plugin is in
			// rather than the one it would have gone to.
			TEST_ASSERT(pm.getInstallPath("Synth-1000FX", "VST3") == elsewhere.getFullPathName().toStdString());

			// Nothing of another product is claimed on the way past.
			TEST_ASSERT(!pm.isInstalled("Synth-2000", "VST3"));
		}

		{
			// Somebody has pointed the format at a folder of their own. What is installed is still
			// what is installed - that is the case that needs the search most, since the build they
			// have is in the standard folder and the one they chose is empty.
			auto pm = makeManager(true);

			std::string error;
			TEST_ASSERT(pm.refresh(error));

			pm.getInstallPaths().set(PluginFormat::VST3, chosen.getFullPathName().toStdString());

			TEST_ASSERT(pm.isInstalled("Synth-1000", "VST3"));

			// But it goes where they said, because they said so.
			TEST_ASSERT(pm.getInstallPath("Synth-1000", "VST3") == chosen.getFullPathName().toStdString());

			// And once it is there, that copy is the one this is about.
			TEST_ASSERT(chosen.getChildFile("Synth-1000.vst3").createDirectory().wasOk());

			TEST_ASSERT(pm.locateInstalled("Synth-1000", PluginFormat::VST3) ==
				chosen.getFullPathName().toStdString());

			TEST_ASSERT(chosen.getChildFile("Synth-1000.vst3").deleteRecursively());
		}

		{
			// A folder of the maker's name under the one the format goes to, which is how a good many
			// people keep their plugins - and with a space in the name, because the report that found
			// this blamed the space. The space was never it: nothing looked under the folder at all.
			//
			// Everything is put there before the first question is asked. The folders under a location
			// are remembered for a second, so a test that makes one and asks in the same breath would be
			// asking about the list taken before it existed.
			const auto own = dir.getChildFile("own");
			const auto nested = own.getChildFile("The Usual Suspects");
			const auto deep = nested.getChildFile("old builds/2.2.25");

			TEST_ASSERT(nested.getChildFile("Synth-2000.vst3").createDirectory().wasOk());
			TEST_ASSERT(deep.getChildFile("Synth-3000.vst3").createDirectory().wasOk());

			// Inside a plugin rather than beside one. A bundle is a folder, and nothing in one is
			// another plugin - walking into them is also most of what a search like this would cost.
			TEST_ASSERT(nested.getChildFile("Synth-2000.vst3/Contents/Synth-4000.vst3").createDirectory().wasOk());

			auto pm = makeManager(true);

			std::string error;
			TEST_ASSERT(pm.refresh(error));

			pm.getInstallPaths().set(PluginFormat::VST3, own.getFullPathName().toStdString());

			TEST_ASSERT(pm.isInstalled("Synth-2000", "VST3"));
			TEST_ASSERT(pm.locateInstalled("Synth-2000", PluginFormat::VST3) == nested.getFullPathName().toStdString());

			// The upgrade lands on top of it. The folder they chose is the one above, but a copy inside
			// it is in the place they chose, so installing into the parent would leave them with two.
			TEST_ASSERT(pm.getInstallPath("Synth-2000", "VST3") == nested.getFullPathName().toStdString());

			// However deep: there is no rule saying people stop at one folder.
			TEST_ASSERT(pm.locateInstalled("Synth-3000", PluginFormat::VST3) == deep.getFullPathName().toStdString());

			TEST_ASSERT(pm.locateInstalled("Synth-4000", PluginFormat::VST3).empty());

			TEST_ASSERT(own.deleteRecursively());
		}

		// A program is not looked for: there is no convention for where one goes, and what an archive
		// unpacks is not named after it either.
		TEST_ASSERT(PlatformUtils::getSearchPaths(PluginFormat::Standalone).empty());

		PlatformUtils::setSearchPaths(PluginFormat::VST3, {});

		// The real list, which starts where an install would go: a copy there is the copy this is
		// about, whatever is lying around elsewhere.
		const auto real = PlatformUtils::getSearchPaths(PluginFormat::VST3);

		TEST_ASSERT(!real.empty());
		TEST_ASSERT(real.front() == PlatformUtils::getDefaultPluginPath(PluginFormat::VST3));

		PlatformUtils::setSettingsFolder({});
		dir.deleteRecursively();
	}

	// Taking several things out behind one prompt (EMU-221).
	//
	// The prompt itself is not something a test can answer, so this is the two halves around it: the
	// worker that runs behind it, which removes a whole list in one go, and the manager, which takes
	// a plugin and its effect build out in one call and says per item what went.
	void testSeveralRemovalsAreOneRun()
	{
		const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("pluginManagerTest-removemany-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

		const auto plugins = root.getChildFile("plugins");

		TEST_ASSERT(plugins.getChildFile("Synth-1000.vst3/Contents").createDirectory().wasOk());
		TEST_ASSERT(plugins.getChildFile("Synth-1000FX.vst3/Contents").createDirectory().wasOk());
		TEST_ASSERT(plugins.getChildFile("changelog_Synth-1000.txt").replaceWithText("notes"));
		TEST_ASSERT(plugins.getChildFile("SomethingElse.vst3").replaceWithText("not ours"));

		// The worker, given a list: every path on it goes, and nothing else does.
		{
			const auto report = root.getChildFile("report.txt");
			const auto jobs = root.getChildFile("jobs.txt");

			TEST_ASSERT(report.replaceWithText({}));
			TEST_ASSERT(jobs.replaceWithText(
				plugins.getChildFile("Synth-1000.vst3").getFullPathName() + "\n" +
				plugins.getChildFile("changelog_Synth-1000.txt").getFullPathName() + "\n"));

			auto exitCode = -1;

			TEST_ASSERT(ElevatedTask::runAsWorker({"--elevated-task", report.getFullPathName().toStdString(),
				"removemany", jobs.getFullPathName().toStdString()}, exitCode));

			TEST_ASSERT(exitCode == 0);
			TEST_ASSERT(!plugins.getChildFile("Synth-1000.vst3").exists());
			TEST_ASSERT(!plugins.getChildFile("changelog_Synth-1000.txt").exists());
			TEST_ASSERT(plugins.getChildFile("Synth-1000FX.vst3").exists());
			TEST_ASSERT(plugins.getChildFile("SomethingElse.vst3").exists());
			TEST_ASSERT(report.loadFileAsString().isEmpty());

			// An empty list is a mistake on our side and says so, rather than reporting success.
			TEST_ASSERT(jobs.replaceWithText({}));
			TEST_ASSERT(ElevatedTask::runAsWorker({"--elevated-task", report.getFullPathName().toStdString(),
				"removemany", jobs.getFullPathName().toStdString()}, exitCode));
			TEST_ASSERT(exitCode != 0);
			TEST_ASSERT(report.loadFileAsString().isNotEmpty());
		}

		// The manager: a plugin and its effect build in one call, one answer each, and the records of
		// both gone with them - and the changelog both claim, which neither would take while the other
		// still had it. One that is not installed fails on its own without taking the others down.
		TEST_ASSERT(plugins.getChildFile("Synth-1000.vst3/Contents").createDirectory().wasOk());
		TEST_ASSERT(plugins.getChildFile("changelog_Synth-1000.txt").replaceWithText("notes"));

		PlatformUtils::setSettingsFolder(root.getFullPathName().toStdString());

		{
			PluginManager pm(false);

			pm.getInstallPaths().set(PluginFormat::VST3, plugins.getFullPathName().toStdString());

			Asset plugin;
			plugin.product = "Synth-1000";
			plugin.format = "VST3";
			plugin.version = "2.2.19";

			auto fx = plugin;
			fx.product = "Synth-1000FX";

			pm.recordInstall(plugin, {"Synth-1000.vst3", "changelog_Synth-1000.txt"});
			pm.recordInstall(fx, {"Synth-1000FX.vst3", "changelog_Synth-1000.txt"});

			std::string failure;

			const auto results = pm.uninstallMany({{"Synth-1000FX", "VST3"}, {"Synth-1000", "VST3"},
				{"Synth-2000", "VST3"}}, failure);

			TEST_ASSERT(results.size() == 3);
			TEST_ASSERT(results[0] && results[1]);
			TEST_ASSERT(!results[2]);
			TEST_ASSERT(failure.find("Synth-2000") != std::string::npos);

			TEST_ASSERT(!plugins.getChildFile("Synth-1000.vst3").exists());
			TEST_ASSERT(!plugins.getChildFile("Synth-1000FX.vst3").exists());
			TEST_ASSERT(!plugins.getChildFile("changelog_Synth-1000.txt").exists());
			TEST_ASSERT(plugins.getChildFile("SomethingElse.vst3").exists());

			TEST_ASSERT(!pm.isInstalled("Synth-1000", "VST3"));
			TEST_ASSERT(!pm.isInstalled("Synth-1000FX", "VST3"));
		}

		PlatformUtils::setSettingsFolder({});
		root.deleteRecursively();
	}

	// The half of an install that runs with administrator rights: a copy of files that are already
	// on the disk, done by a second copy of this program started through the prompt.
	//
	// Nothing here is elevated - it runs the worker directly, which is the same code that runs on the
	// other side of the prompt. What the prompt itself does is not something a test can click.
	void testElevatedWorker()
	{
		const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("pluginManagerTest-elevated-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

		const auto source = root.getChildFile("staging");
		const auto target = root.getChildFile("plugins");

		TEST_ASSERT(source.getChildFile("Synth-1000.vst3/Contents").createDirectory().wasOk());
		TEST_ASSERT(source.getChildFile("Synth-1000.vst3/Contents/binary").replaceWithText("new"));
		TEST_ASSERT(source.getChildFile("changelog.txt").replaceWithText("new"));

		// Something of an older install is already there, and a file beside it that is nobody's
		// business: what is unpacked replaces its own files and leaves the rest alone.
		TEST_ASSERT(target.createDirectory().wasOk());
		TEST_ASSERT(target.getChildFile("changelog.txt").replaceWithText("old"));
		TEST_ASSERT(target.getChildFile("SomethingElse.vst3").replaceWithText("not ours"));

		// Where the worker says what went wrong. It is the only way back: the process that asked
		// for the rights is waiting on an exit code and has no pipe to it.
		const auto report = root.getChildFile("report.txt");

		TEST_ASSERT(report.replaceWithText({}));

		const auto reportPath = report.getFullPathName().toStdString();

		const auto command = std::vector<std::string>{"--elevated-task", reportPath, "copy",
			source.getFullPathName().toStdString(), target.getFullPathName().toStdString()};

		auto exitCode = -1;

		TEST_ASSERT(ElevatedTask::runAsWorker(command, exitCode));
		TEST_ASSERT(exitCode == 0);

		TEST_ASSERT(target.getChildFile("Synth-1000.vst3/Contents/binary").loadFileAsString() == "new");
		TEST_ASSERT(target.getChildFile("changelog.txt").loadFileAsString() == "new");
		TEST_ASSERT(target.getChildFile("SomethingElse.vst3").loadFileAsString() == "not ours");

		// Nothing to report when it worked.
		TEST_ASSERT(report.loadFileAsString().isEmpty());

		// The other half: taking one thing away again.
		const auto remove = std::vector<std::string>{"--elevated-task", reportPath, "remove",
			target.getChildFile("Synth-1000.vst3").getFullPathName().toStdString()};

		TEST_ASSERT(ElevatedTask::runAsWorker(remove, exitCode));
		TEST_ASSERT(exitCode == 0);
		TEST_ASSERT(!target.getChildFile("Synth-1000.vst3").exists());
		TEST_ASSERT(target.getChildFile("changelog.txt").exists());

		// And a copy that cannot finish. What comes back must name the file rather than only say
		// that something did not work: behind the prompt there is nobody to ask.
		const auto held = target.getChildFile("changelog.txt");

		TEST_ASSERT(held.setReadOnly(true));

		// A process that writes through a read-only bit has nothing to prove here.
		if (!juce::FileOutputStream(held).openedOk())
		{
			TEST_ASSERT(ElevatedTask::runAsWorker(command, exitCode));
			TEST_ASSERT(exitCode != 0);
			TEST_ASSERT(report.loadFileAsString().toStdString().find(held.getFullPathName().toStdString())
				!= std::string::npos);
		}

		TEST_ASSERT(held.setReadOnly(false));

		// A command line that is not the one this program sends itself is not one it acts on, and
		// the switch on its own does nothing either.
		TEST_ASSERT(!ElevatedTask::runAsWorker({"install", "Synth-1000"}, exitCode));
		TEST_ASSERT(!ElevatedTask::runAsWorker({}, exitCode));

		TEST_ASSERT(ElevatedTask::runAsWorker({"--elevated-task"}, exitCode));
		TEST_ASSERT(exitCode != 0);

		// A verb without everything it needs is not acted on half way, and says so.
		TEST_ASSERT(report.replaceWithText({}));
		TEST_ASSERT(ElevatedTask::runAsWorker({"--elevated-task", reportPath, "copy",
			source.getFullPathName().toStdString()}, exitCode));
		TEST_ASSERT(exitCode != 0);
		TEST_ASSERT(report.loadFileAsString().isNotEmpty());

		root.deleteRecursively();
	}

	// Uninstalling removes what an install put there - the one path it wrote, whether that is a file
	// or a bundle - and forgets it was ever installed.
	void testUninstallRemovesWhatWasInstalled()
	{
		const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("pluginManagerTest-uninstall-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

		TEST_ASSERT(dir.createDirectory().wasOk());

		const auto path = dir.getFullPathName().toStdString();

		// A bundle rather than a plain file, which is what most formats are.
		const auto bundle = juce::File(juce::String(PlatformUtils::installedFilePath("Synth-1000", PluginFormat::VST3, path)));

		TEST_ASSERT(bundle.getChildFile("Contents").createDirectory().wasOk());
		TEST_ASSERT(bundle.getChildFile("Contents/binary").create().wasOk());
		TEST_ASSERT(PlatformUtils::isPluginInstalled("Synth-1000", PluginFormat::VST3, path));

		std::string message;

		TEST_ASSERT(PluginInstaller::uninstall("Synth-1000", "Synth-1000", PluginFormat::VST3, path, {}, message));
		TEST_ASSERT(!bundle.exists());
		TEST_ASSERT(!PlatformUtils::isPluginInstalled("Synth-1000", PluginFormat::VST3, path));

		// Removing what is not there says so rather than claiming to have done something.
		TEST_ASSERT(!PluginInstaller::uninstall("Synth-1000", "Synth-1000", PluginFormat::VST3, path, {}, message));
		TEST_ASSERT(message.find("not installed") != std::string::npos);

		// And the record goes with it.
		Asset asset;
		TEST_ASSERT(parseAssetName("TheUsualSuspects-Synth-1000-VST3-2.2.19-win64.zip", asset));

		const auto file = juce::File(juce::String(PlatformUtils::installedFilePath("Synth-1000", PluginFormat::VST3, path)));
		TEST_ASSERT(file.create().wasOk());

		InstalledPlugins installed(false);
		installed.set(asset, path);

		TEST_ASSERT(installed.getVersion("Synth-1000", PluginFormat::VST3, path) == "2.2.19");

		installed.remove("Synth-1000", PluginFormat::VST3);

		TEST_ASSERT(installed.getVersion("Synth-1000", PluginFormat::VST3, path).empty());

		dir.deleteRecursively();
	}


	// The team login is switched on by a line somebody puts in the settings file by hand, so this
	// program has to leave alone what it did not write - including the line that hides it.
	void testSettingsKeepWhatTheyDoNotOwn()
	{
		const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("pluginManagerTest-settings-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

		TEST_ASSERT(dir.createDirectory().wasOk());

		PlatformUtils::setSettingsFolder(dir.getFullPathName().toStdString());

		const auto file = juce::File(juce::String(PlatformUtils::getSettingsPath("settings.xml")));

		TEST_ASSERT(file.replaceWithText(
			"<pluginManager>"
			"<teamLogin enabled=\"1\"/>"
			"<installPath format=\"VST3\" path=\"/from/the/file\"/>"
			"<somethingElse note=\"from a later version\"/>"
			"</pluginManager>"));

		{
			InstallPaths paths;

			TEST_ASSERT(paths.hasTeamLogin());
			TEST_ASSERT(paths.get(PluginFormat::VST3) == "/from/the/file");

			// Writing the file back is what used to be the danger.
			paths.set(PluginFormat::CLAP, "/somewhere/else");
			paths.save();
		}

		const auto written = file.loadFileAsString();

		TEST_ASSERT(written.contains("teamLogin"));
		TEST_ASSERT(written.contains("somethingElse"));
		TEST_ASSERT(written.contains("/somewhere/else"));

		// And it reads back the same way.
		{
			const InstallPaths paths;

			TEST_ASSERT(paths.hasTeamLogin());
			TEST_ASSERT(paths.get(PluginFormat::CLAP) == "/somewhere/else");
		}

		// No line, no team login.
		TEST_ASSERT(file.replaceWithText("<pluginManager><installPath format=\"VST3\" path=\"/x\"/></pluginManager>"));

		{
			const InstallPaths paths;
			TEST_ASSERT(!paths.hasTeamLogin());
		}

		// A settings file from before there was one list. <view> was this program's own setting, so
		// it is read and dropped rather than kept with what the program does not recognise - and it
		// leaves the file the first time anything writes it, instead of being carried for ever.
		TEST_ASSERT(file.replaceWithText(
			"<pluginManager>"
			"<view compact=\"1\"/>"
			"<installPath format=\"VST3\" path=\"/x\"/>"
			"<somethingElse note=\"still not ours\"/>"
			"</pluginManager>"));

		{
			InstallPaths paths;
			paths.save();
		}

		{
			const auto written2 = file.loadFileAsString();

			TEST_ASSERT(!written2.contains("<view"));

			// What was not ours is still there, which is the half of this that must not change.
			TEST_ASSERT(written2.contains("somethingElse"));
			TEST_ASSERT(written2.contains("/x"));
		}

		PlatformUtils::setSettingsFolder({});

		dir.deleteRecursively();
	}

	void testWriteCheckExplainsItself()
	{
		std::string error;

		TEST_ASSERT(!ZipExtractor::canWriteTo({}, error));
		TEST_ASSERT(error.find("Preferences") != std::string::npos);

		const auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("pluginManagerTest-write-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

		TEST_ASSERT(temp.createDirectory().wasOk());
		TEST_ASSERT(ZipExtractor::canWriteTo(temp.getFullPathName().toStdString(), error));

		// Nothing is left behind by the check itself.
		TEST_ASSERT(temp.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0);

		// A folder that cannot be created, because what it would sit in is a file. The same answer
		// the plugin folders give an unelevated process, without needing one.
		const auto file = temp.getChildFile("a-file");
		TEST_ASSERT(file.create().wasOk());

		TEST_ASSERT(!ZipExtractor::canWriteTo(file.getChildFile("sub").getFullPathName().toStdString(), error));
		TEST_ASSERT(error.find("administrator") != std::string::npos);

		// A location that is a file outright says so rather than talking about permissions.
		TEST_ASSERT(!ZipExtractor::canWriteTo(file.getFullPathName().toStdString(), error));
		TEST_ASSERT(error.find("not a folder") != std::string::npos);

		temp.deleteRecursively();
	}

	// The folder a plugin reads its ROM from, which is made when the plugin is installed so that
	// there is somewhere to put one before the plugin has ever run. It is only any use if it is
	// the folder the plugin itself will look in, so what is pinned here is what decides that.
	void testRomFolder()
	{
		const auto osirus = PlatformUtils::romFolder("Osirus");

		TEST_ASSERT(osirus.find("The Usual Suspects") != std::string::npos);
		TEST_ASSERT(osirus.find("Osirus") != std::string::npos);

		// Ends in roms, then whichever separator this system writes.
		TEST_ASSERT(osirus.size() > 5 && osirus.substr(osirus.size() - 5, 4) == "roms");

		// One folder per product, which is the whole point of naming it after one.
		TEST_ASSERT(PlatformUtils::romFolder("Vavra") != osirus);
	}

	// Which product's folder in Documents a build uses. An effect build uses the plugin's, which is what
	// the plugin does too. A product whose own name ends in FX is no effect build of anything: taking
	// that name apart left an empty folder named after what came before the FX.
	void testAnEffectBuildUsesThePluginsFolder()
	{
		Catalog c;

		Product synth;
		synth.product = "Synth-1000";

		Product named;
		named.product = "WaveFX";

		Asset fx;
		fx.product = "Synth-1000FX";

		Asset own;
		own.product = "WaveFX";

		// Assets first, as a refresh adds them before the lists it learns of later: the folder has to
		// follow when the products arrive.
		c.addAssets({fx, own});
		TEST_ASSERT(c.getAssets()[0].getDataProduct() == "Synth-1000FX");

		c.setProducts({synth, named});

		TEST_ASSERT(c.dataProductOf("Synth-1000") == "Synth-1000");
		TEST_ASSERT(c.dataProductOf("Synth-1000FX") == "Synth-1000");
		TEST_ASSERT(c.dataProductOf("WaveFX") == "WaveFX");

		// Other variants are programs with folders of their own, and nothing in a list is its own.
		TEST_ASSERT(c.dataProductOf("Synth-1000TestConsole") == "Synth-1000TestConsole");
		TEST_ASSERT(c.dataProductOf("Unknown") == "Unknown");

		TEST_ASSERT(c.getAssets()[0].getDataProduct() == "Synth-1000");
		TEST_ASSERT(c.getAssets()[1].getDataProduct() == "WaveFX");

		// And the folder is the name it is given, whole.
		TEST_ASSERT(PlatformUtils::romFolder("WaveFX").find("WaveFX") != std::string::npos);
	}

	// Installing a plugin also makes the folder it will read its ROM from, so that there is
	// somewhere to put one before the plugin has ever been started.
	//
	// Not for a program that belongs to a plugin, though. A test console looks beside its own
	// executable and in the working directory, never in Documents, so one made there is a folder
	// nothing ever opens - and it turned up named after the console however the console itself had
	// been installed, which is what made it look like the install had gone to the wrong place.
	void testInstallMakesTheRomFolder()
	{
		const Fixture fixture;

		const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("pluginManagerTest-roms-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

		const auto content = root.getChildFile("content");
		const auto target = root.getChildFile("plugins");
		const auto data = root.getChildFile("data");

		TEST_ASSERT(content.getChildFile("Synth-1000FX.vst3/Contents").createDirectory().wasOk());
		TEST_ASSERT(content.getChildFile("Synth-1000FX.vst3/Contents/binary").replaceWithText("x"));

		const auto zip = root.getChildFile("build.zip");

		makeZip(content, zip);

		setDataFolder(data.getFullPathName().toStdString());

		// Its own folder, or writing down an install would land in the settings of whoever is
		// running this.
		PlatformUtils::setSettingsFolder(root.getChildFile("settings").getFullPathName().toStdString());

		// A catalog that knows Synth-1000, because which product a build belongs to is what decides
		// this, and a console is not a product of its own.
		auto pm = makeManager(true);
		std::string error;
		TEST_ASSERT(pm.refresh(error));

		// An effect build, so that what is checked is also the folder it shares rather than one of
		// its own: OsirusFX reads Osirus's ROM, and the plugin resolves it that way itself.
		Asset asset;
		asset.product = "Synth-1000FX";
		asset.format = "VST3";
		asset.version = "2.2.26";
		asset.url = "file:///" + zip.getFullPathName().toStdString();

		PluginInstaller installer;
		std::string message;

		TEST_ASSERT(installer.install(asset, {}, target.getFullPathName().toStdString(), {}, message));
		TEST_ASSERT(target.getChildFile("Synth-1000FX.vst3/Contents/binary").existsAsFile());

		pm.recordInstall(asset, {});

		TEST_ASSERT(data.getChildFile("The Usual Suspects/Synth-1000/roms").isDirectory());
		TEST_ASSERT(!data.getChildFile("The Usual Suspects/Synth-1000FX").exists());

		// No program gets one, wherever it was installed to - neither one belonging to a plugin,
		// which is listed as a product of its own so that it has a row...
		Asset console;
		console.product = "Synth-1000TestConsole";
		console.format = "Standalone";
		console.version = "2.2.26";

		pm.recordInstall(console, {});

		TEST_ASSERT(!data.getChildFile("The Usual Suspects/Synth-1000TestConsole").exists());

		// ...nor one that belongs to nothing, like the bridge server, which makes its own folders
		// under a name of its own choosing the first time it runs.
		Asset server;
		server.product = "Prototype-9";
		server.format = "Standalone";
		server.version = "2.2.26";

		pm.recordInstall(server, {});

		TEST_ASSERT(!data.getChildFile("The Usual Suspects/Prototype-9").exists());

		PlatformUtils::setSettingsFolder({});
		setDataFolder({});
		root.deleteRecursively();
	}

	// An LV2 is a folder with a bare library in it and the turtle files that describe it. macOS asks
	// CFBundle, which wants a bundle with an Info.plist, and an LV2 is not one; an ELF carries
	// nothing either. So the turtle is the only thing that answers on two of the three systems, and
	// on Windows it says the same as the resource in the library.
	void testAnLv2SaysWhichVersionItIs()
	{
		const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("pluginManagerTest-lv2-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

		const auto bundle = root.getChildFile("Synth-1000.lv2");

		TEST_ASSERT(bundle.createDirectory().wasOk());

		// The shape JUCE writes, down to where the revision sits.
		TEST_ASSERT(bundle.getChildFile("dsp.ttl").replaceWithText(
			"<http://theusualsuspects.lv2/Synth-1000>\n"
			"\ta lv2:InstrumentPlugin ;\n"
			"\tdoap:name \"Synth-1000\" ;\n"
			"\tlv2:minorVersion 2 ;\n"
			"\tlv2:microVersion 19 ;\n"
			"\tdoap:release [\n"
			"\t\ta doap:Version ;\n"
			"\t\tdoap:revision \"2.2.19\" ;\n"
			"\t] .\n"));

		// The manifest names the binary and says nothing about a version, so it must not stop the
		// search at the first file read.
		TEST_ASSERT(bundle.getChildFile("manifest.ttl").replaceWithText(
			"<http://theusualsuspects.lv2/Synth-1000>\n"
			"\ta lv2:Plugin ;\n"
			"\tlv2:binary <Synth-1000.so> .\n"));

		TEST_ASSERT(PlatformUtils::getInstalledVersion("Synth-1000", PluginFormat::LV2,
			root.getFullPathName().toStdString()) == "2.2.19");

		// A turtle naming no revision is not an answer, and must not be read as one.
		const auto bare = root.getChildFile("Synth-2000.lv2");

		TEST_ASSERT(bare.createDirectory().wasOk());
		TEST_ASSERT(bare.getChildFile("manifest.ttl").replaceWithText("\ta lv2:Plugin .\n"));

		TEST_ASSERT(PlatformUtils::getInstalledVersion("Synth-2000", PluginFormat::LV2,
			root.getFullPathName().toStdString()).empty());

		// Nothing installed there at all stays nothing.
		TEST_ASSERT(PlatformUtils::getInstalledVersion("Synth-3000", PluginFormat::LV2,
			root.getFullPathName().toStdString()).empty());

		root.deleteRecursively();
	}

	void testInstalledFileName()
	{
		TEST_ASSERT(PlatformUtils::pluginFileName("Osirus", PluginFormat::VST3) == "Osirus.vst3");
		TEST_ASSERT(PlatformUtils::pluginFileName("Osirus", PluginFormat::CLAP) == "Osirus.clap");
		TEST_ASSERT(PlatformUtils::pluginFileName("Osirus", PluginFormat::LV2) == "Osirus.lv2");
		TEST_ASSERT(PlatformUtils::pluginFileName("Osirus", PluginFormat::AU) == "Osirus.component");

		// Standalone is a program rather than a plugin, so what it is called depends on the platform -
		// but it has a name, which is what makes it installable and removable like the rest.
		TEST_ASSERT(!PlatformUtils::pluginFileName("Osirus", PluginFormat::Standalone).empty());
		TEST_ASSERT(PlatformUtils::pluginFileName("Osirus", PluginFormat::Standalone).find("Osirus") == 0);

		// A format with no location has nothing to point at.
		TEST_ASSERT(PlatformUtils::installedFilePath("Osirus", PluginFormat::VST3, {}).empty());
		TEST_ASSERT(PlatformUtils::pluginFileName("Osirus", PluginFormat::Count).empty());

		const auto path = PlatformUtils::installedFilePath("Osirus", PluginFormat::VST3, "/plugins");

		TEST_ASSERT(!path.empty());
		TEST_ASSERT(path.find("Osirus.vst3") != std::string::npos);
		TEST_ASSERT(!PlatformUtils::isPluginInstalled("Osirus", PluginFormat::VST3, "/no/such/place"));
	}
}

int main()
{
	try
	{
		testPluginAsset();
		testPlatforms();
		testVariantsAreProducts();
		testProductNamesWithDashes();
		testStandaloneFormat();
		testFormatlessAsset();
		testRejectsForeignNames();
		testVersionOrdering();
		testInstallActions();
		testPairedInstallActions();
		testEffectBuildIsIncludedWhenAskedForOrInstalled();
		testAColumnFetchesOnlyWhatNeedsIt();
		testDirectoryIndex();
		testDirectoryIndexDoesNotDuplicate();
		testEmptyDirectoryIndex();
		testVersionList();
		testFormatList();
		testALoginCanBeHandedOverInAFile();
		testVariantsFound();
		testAnEffectBuildCanExistForOnlySomeVersions();
		testPrefersZip();
		testUniversalMatchesAnyArch();
		testChannelPreference();
		testAddProductsSkipsKnown();
		testAddProductsKeepsTheFirstCategory();
		testProductsSortByManufacturerThenYear();
		testACategoryIsReadFromTheProductLists();
		testAnEmptySlugMeansNoFolderOfItsOwn();

		testATeamLoginThatIsTurnedOffIsNotSent();
		testElevatedCopiesAreCollectedAndRecordedOnlyWhenMade();
		testOnlyACopyThatNeedsRightsIsCollected();
		testHttpDatesBecomeSeconds();
		testARateLimitedRequestWaitsAsLongAsItIsTold();
		testADirectoryListingCarriesTheDate();
		testABuildOfTheSameVersionCanStillBeNewer();
		testRefreshMergesChannels();
		testPublicBuildsPreferOurOwnCopy();
		testProtectedChannelsNeedALogin();
		testHttpFailuresAreToldApart();
		testALoginForAnEmptyFolderIsRefused();
		testADonatorFolderCanDescribeItself();
		testAThumbnailLoginGoesOnlyToOurServer();
		testRefreshSurvivesAMissingSource();
		testCredentialsFollowTheOwningProduct();
		testDonatorCredentialsFollowTheFolder();
		testEffectBuildsFollowTheirPlugin();
		testCompressedBodiesAreRead();
		testDownloadWritesTheFile();

		testInstallPathOverrides();
		testFormatsAreTheOnesThisSystemHas();
		testEveryFormatHasSomewhereToGo();
		testRomFolder();
		testAnEffectBuildUsesThePluginsFolder();
		testInstallMakesTheRomFolder();
		testAnLv2SaysWhichVersionItIs();
		testInstalledFileName();
		testWriteCheckExplainsItself();
		testVersionIsReadFromInsideABundle();
		testInstalledVersionsAreRemembered();
		testPerProductPathsWin();
		testUninstallRemovesWhatWasInstalled();
		testAProgramKeepsTheNameItWasBuiltUnder();
		testAnEffectProgramIsFoundBesideItsPlugin();
		testAnInstalledEffectBuildKeepsUpWithItsPlugin();
		testUninstallTakesWhatTheArchiveBrought();
		testAnInstallLeftBehindIsStillFound();
		testAnInstalledBuildIsFoundWhereverItIs();
		testStoredLoginsAreNotLyingInTheOpen();
		testADonatorFolderMayBeTypedAsALink();
		testElevatedWorker();
		testSeveralRemovalsAreOneRun();
		testAProgramIsFoundWithoutKnowingItsName();
		testAnUpdateKeepsWhatTheUserPutInTheBundle();
		testTheTeamLoginFlagSurvivesBeingWrittenBack();
		testBackupAndRestoreOfADataFolder();
		testBackupsGoWhereTheyArePointed();
		testAnUpdateMovesTheRomOutOfTheBundle();
		testUninstallKeepsTheRomItFinds();
		testAnUpdateIsOnlyOfferedForANewerBuild();
		testInstallFlattensAnArchivePrefix();
		testAnArchiveWithNothingToInstallFails();
		testInstallLeavesTheOldBuildAloneWhenItCannotFinish();
		testSettingsKeepWhatTheyDoNotOwn();
		testAChangelogIsReadPerProductAndVersion();
		testAChangelogComesFromTheNewestPlaceThereIsOne();
		testAdditionalFilesGoIntoTheDataFolder();
		testACopyWithoutAVersionIsJudgedByItsDate();
		testAProductWithAnOlderInstallHasAnUpdate();
		testAHandInstalledCopyIsOfferedALaterBuildOfItsVersion();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}

	std::cout << "pluginManagerTest: all tests passed\n";
	return 0;
}
