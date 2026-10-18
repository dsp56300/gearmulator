#pragma once

#include <string>
#include <vector>

#include "juce_core/juce_core.h"

namespace pluginManager
{
	// Everything a product keeps in Documents/The Usual Suspects/<product>/ - its config, its
	// patches, its skins and its ROMs - as one zip that can be put back.
	//
	// Deliberately not the plugin. That can be downloaded again and this program already installs
	// it; the data folder is the half that cannot, which is also why an uninstall leaves it alone.
	class Backup
	{
	public:
		struct Entry
		{
			std::string product;
			std::string file;
			juce::Time time;
			juce::int64 size = 0;
		};

		using ProgressCallback = std::function<void(float _progress)>;
		using CompletionCallback = std::function<void(bool _success, const std::string& _message)>;

		// Documents/The Usual Suspects/PluginManager/backups/, beside this program's own config,
		// unless Preferences point somewhere else: see InstallPaths::getBackupFolder.
		static std::string folder();

		// _file comes back holding what was written. Fails when the product has nothing in its data
		// folder, because an empty backup is a file that only looks like a safety net.
		static bool create(const std::string& _product, std::string& _file, std::string& _error);

		// Newest first, by the time in the name rather than the file's own: a copied backup keeps
		// the former and not the latter. An empty product lists every product's.
		static std::vector<Entry> list(const std::string& _product = {});

		// Puts a backup back into the product's data folder.
		//
		// Over the top rather than into an emptied folder: what the backup holds wins, and anything
		// added since is left where it is. The alternative is deleting files the user never asked
		// about, and a restore that quietly takes away a ROM added last week is the bug this whole
		// feature exists to avoid.
		//
		// A backup of the current state is taken first, so a restore of the wrong thing is itself
		// undoable. That one fails harmlessly when there is nothing there yet.
		static bool restore(const std::string& _file, const std::string& _product, std::string& _error,
			const ProgressCallback& _onProgress = {});

		static bool remove(const std::string& _file, std::string& _error);

		// The same two on a worker thread, answering on the message thread. A product's folder runs
		// to hundreds of megabytes - OsTIrus is 374 - and doing either of these where the window is
		// drawn freezes it for as long as it takes.
		static void createAsync(const std::string& _product, CompletionCallback _onComplete);

		// Progress is per file in the archive, which is a real measure because unpacking is a loop
		// this owns. Backing up has none: JUCE writes the whole zip in one call and offers only a
		// double it updates from inside it, which would have to be read across threads for a number
		// nobody acts on. So a backup says that it is working and nothing more.
		static void restoreAsync(const std::string& _file, const std::string& _product,
			ProgressCallback _onProgress, CompletionCallback _onComplete);

		// What a backup of this product would cost, so the caller can say so before starting. Zero
		// when there is nothing to back up.
		static juce::int64 sizeOf(const std::string& _product);
	};
}
