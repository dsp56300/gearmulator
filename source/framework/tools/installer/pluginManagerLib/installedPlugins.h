#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "asset.h"
#include "platformUtils.h"

namespace pluginManager
{
	// Which version of a plugin is on disk.
	//
	// Two sources, because neither covers everything. What this program installed is written down
	// here, which is exact and works for every format on every platform. Anything installed before
	// or by other means is asked of the file itself, which only some formats can answer - see
	// PlatformUtils::getInstalledVersion.
	class InstalledPlugins
	{
	public:
		struct Entry
		{
			std::string product;
			std::string format;
			std::string version;
			std::string path;		// what was installed, so a file that is gone is not reported
			std::string time;		// when, so that "I never installed that" is a question with an answer

			// How old the file was that this came from, which is how a channel that reuses its version
			// numbers can still say whether what is on the server now is something else. Both are the
			// server's own clock rather than this machine's: a user whose clock runs slow would
			// otherwise be offered an update for the build they are already running.
			//
			// listed is comparable only with another listing, modified only with another Last-Modified,
			// and both are empty for a build installed before any of this, or from a public release.
			std::string listed;
			int64_t modified = 0;

			// Everything the archive put into that folder, the plugin included, by name. Empty for an
			// install from before these were written down, and then only the plugin can be removed.
			std::vector<std::string> files;
		};

		explicit InstalledPlugins(bool _load = true);

		// Records what an install put on disk. _dir is the location the format is installed to, and
		// _files everything the archive left in it.
		void set(const Asset& _asset, const std::string& _dir, std::vector<std::string> _files = {},
		         int64_t _modified = 0);

		// The full paths an uninstall of this should remove: what that install wrote, less anything
		// another install still claims in the same folder. Two products do bring the same file -
		// every effect build carries the changelog of the plugin it belongs to - and taking one away
		// must not take the other's copy of it.
		//
		// _dir is the folder being emptied, and a record of an install somewhere else answers with
		// nothing: those names belong to that other folder, and deleting them out of this one would
		// take files no install of ours ever put here.
		//
		// _alsoGoing are the installs removed in the same run: their claims do not count, or a plugin and
		// its effect build taken out together would each leave the changelog to the other (EMU-221).
		//
		// Empty when nothing was written down, which is what says to fall back to the plugin alone.
		std::vector<std::string> filesToRemove(const std::string& _product, PluginFormat _format,
		                                       const std::string& _dir,
		                                       const std::vector<std::pair<std::string, PluginFormat>>& _alsoGoing = {}) const;

		// The file that says this product and format are installed in this folder, empty when none is.
		//
		// What the install actually wrote, because the archive decides the name: a program keeps the
		// one it was built under - OsirusTestConsole ships virusTestConsole.exe - so looking for a
		// file named after the product finds nothing however well the install went. Falls back to
		// that name, which is the only thing that can answer for a plugin installed by hand.
		std::string installedFile(const std::string& _product, PluginFormat _format,
		                          const std::string& _dir) const;

		// Empty when nothing is installed or the version cannot be told.
		std::string getVersion(const std::string& _product, PluginFormat _format, const std::string& _dir) const;

		// The record of an install of this product and format into this folder, or nothing. The folder
		// is part of the match: a format pointed somewhere else since is not the build installed then.
		const Entry* findEntry(const std::string& _product, PluginFormat _format,
		                       const std::string& _dir) const;

		// Everything written down, for a caller that has the other half of the question. Whether a
		// record is still where its format goes is not something this can tell - that needs the
		// locations, which is why PluginManager::getStrayInstalls lives there and not here.
		const std::vector<Entry>& getEntries() const { return m_entries; }

		// After an uninstall: what is no longer there is no longer recorded.
		void remove(const std::string& _product, PluginFormat _format);

		void load();
		void save() const;

	private:
		std::vector<Entry> m_entries;
	};
}
