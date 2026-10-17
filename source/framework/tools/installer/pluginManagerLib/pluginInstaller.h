#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "asset.h"
#include "credentials.h"
#include "elevatedBatch.h"
#include "platformUtils.h"

namespace pluginManager
{
	// Downloads one build and unpacks it into the install location for its format.
	//
	// Which file to fetch is already decided by the catalog, so this no longer has to pick a
	// version or a platform - it is handed the asset and installs exactly that.
	class PluginInstaller
	{
	public:
		using ProgressCallback = std::function<void(float _progress)>;
		using CompletionCallback = std::function<void(bool _success, const std::string& _message)>;

		// Where an install puts the copy it is not allowed to make itself. Shared, because an install
		// runs on a worker thread and the run that collects them outlives it.
		//
		// Empty means "ask now, as before", which is what a single install does: one build is one
		// prompt either way, and asking at the end of a run of one would only make it later.
		using Pending = std::shared_ptr<ElevatedBatch>;

		// Blocking. _targetDir is where this format is installed, which is a setting rather than
		// something this decides - see InstallPaths. The login is only needed for a build from a
		// protected channel; public builds pass an empty one. _message is what went wrong, or what
		// was installed.
		// _archiveFile is the name to find the build by inside the archive, for a build that is not
		// named after its product - this program updating itself is the one of those. Empty takes
		// the product's own name, which is what every plugin is called.
		bool install(const Asset& _asset, const Credentials& _credentials, const std::string& _targetDir,
		             const ProgressCallback& _onProgress, std::string& _message,
		             const std::string& _archiveFile = {}, const Pending& _pending = {});

		// For the GUI: installs on a worker thread and reports on the JUCE message thread. The worker
		// holds the state it needs, so this object may be destroyed while an install is still running.
		void installAsync(const Asset& _asset, const Credentials& _credentials, const std::string& _targetDir,
		                  ProgressCallback _onProgress, CompletionCallback _onComplete,
		                  const std::string& _archiveFile = {}, const Pending& _pending = {});

		// Deletes what an install of this format put there. Blocking, and quick: it is a delete per
		// file. _message says what went wrong, which is usually that a host has the plugin open.
		//
		// _files are the full paths that install wrote, from the manifest. An empty list is an install
		// from before those were written down, and then the plugin's own path is all that can go.
		//
		// What only an administrator may remove is gathered rather than asked about file by file: with
		// _needRights given it is added to that list and left for the caller to remove with everything
		// else of the same run behind one prompt, and true means queued as much as gone. Without it,
		// the call asks once at its end for all of its own files.
		//
		// _plugin comes back as the path of the plugin itself, the one file whose being gone means it
		// is uninstalled - a changelog another install still claims stays, and is no reason to say so.
		//
		// _dataProduct is the product whose roms folder takes a ROM found inside the plugin - see
		// Asset::dataProduct.
		static bool uninstall(const std::string& _product, const std::string& _dataProduct, PluginFormat _format,
		                      const std::string& _dir,
		                      const std::vector<std::string>& _files, std::string& _message,
		                      std::vector<std::string>* _needRights = nullptr, std::string* _plugin = nullptr);

		void cancel() const { *m_cancel = true; }

		bool isInstalling() const { return *m_installing; }

		// What the last install put into the folder, for whoever writes the install down.
		const std::vector<std::string>& lastInstalledFiles() const { return *m_lastFiles; }

	private:
		using State = std::shared_ptr<std::atomic<bool>>;

		using Files = std::shared_ptr<std::vector<std::string>>;

		static bool run(const Asset& _asset, const Credentials& _credentials, const std::string& _targetDir,
		                const std::string& _archiveFile,
		                const ProgressCallback& _onProgress, const std::shared_ptr<bool>& _cancel,
		                const State& _installing, const Files& _files, std::string& _message,
		                const Pending& _pending);

		// Shared rather than held, because an install outlives this object: the worker writes here
		// and whoever is still around reads it.
		Files m_lastFiles = std::make_shared<std::vector<std::string>>();

		std::shared_ptr<bool> m_cancel = std::make_shared<bool>(false);
		State m_installing = std::make_shared<std::atomic<bool>>(false);
	};
}
