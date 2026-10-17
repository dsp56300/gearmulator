#pragma once

#include <functional>
#include <memory>
#include <string>

#include "pluginManagerLib/pluginInstaller.h"
#include "pluginManagerLib/pluginManager.h"

namespace juceRmlUi { class RmlComponent; }

namespace pluginManager
{
	// Installing one build, the same way wherever it is started from: the list and the detail page
	// both go through here, so both take the login and the location from the same place, record what
	// they installed, and report a failure in the same words.
	//
	// The callbacks arrive on the message thread with RmlUi access already taken, and are dropped
	// once this object is gone.
	class InstallTask
	{
	public:
		using ProgressCallback = std::function<void(float _progress)>;
		using DoneCallback = std::function<void(bool _success, const std::string& _message)>;

		InstallTask(PluginManager& _pluginManager, juceRmlUi::RmlComponent& _rmlComponent);
		~InstallTask();

		bool isRunning() const { return m_installer.isInstalling(); }
		void cancel() const { m_installer.cancel(); }

		// _productName is what a failure is reported under, which is the name the user sees.
		// _pending collects the copies that need administrator rights instead of asking for them one
		// at a time. An install that goes on it is NOT written down here - it has not happened yet -
		// and PluginManager::finishPending does that once the whole run has been copied into place.
		void start(const Asset& _asset, std::string _productName, ProgressCallback _onProgress, DoneCallback _onDone,
		           PluginInstaller::Pending _pending = {});

	private:
		PluginManager& m_pluginManager;
		juceRmlUi::RmlComponent& m_rmlComponent;
		PluginInstaller m_installer;

		// An install outlives whatever started it - a row is replaced by a refresh, a page moves on -
		// so what it reports is dropped once that is gone.
		std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
	};
}
