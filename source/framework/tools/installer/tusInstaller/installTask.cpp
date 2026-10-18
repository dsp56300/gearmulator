#include "installTask.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlInterfaces.h"

#include <JuceHeader.h>

namespace pluginManager
{
	InstallTask::InstallTask(PluginManager& _pluginManager, juceRmlUi::RmlComponent& _rmlComponent)
		: m_pluginManager(_pluginManager)
		, m_rmlComponent(_rmlComponent)
	{
	}

	InstallTask::~InstallTask()
	{
		*m_alive = false;
	}

	void InstallTask::start(const Asset& _asset, std::string _productName, ProgressCallback _onProgress, DoneCallback _onDone,
	                        PluginInstaller::Pending _pending)
	{
		const auto asset = _asset;

		// How many copies were already waiting. If this install adds one, it was promised rather than
		// made, and writing it down now would record a version that is not on disk yet.
		const auto waiting = _pending ? _pending->size() : 0;

		m_installer.installAsync(asset, m_pluginManager.credentialsFor(asset), m_pluginManager.installPathFor(asset),
			[this, alive = m_alive, onProgress = std::move(_onProgress)](const float _progress)
			{
				if (!*alive || !onProgress)
					return;

				{
					juceRmlUi::RmlInterfaces::ScopedAccess sa(m_rmlComponent);
					onProgress(_progress);
				}

				m_rmlComponent.enqueueUpdate();
			},
			[this, alive = m_alive, asset, productName = std::move(_productName), onDone = std::move(_onDone),
			 pending = _pending, waiting]
			(const bool _success, const std::string& _message)
			{
				if (!*alive)
					return;

				const auto report = [this, productName, onDone](const bool _ok, const std::string& _text)
				{
					{
						juceRmlUi::RmlInterfaces::ScopedAccess sa(m_rmlComponent);

						if (onDone)
							onDone(_ok, _text);
					}

					m_rmlComponent.enqueueUpdate();

					// Said out loud, outside the document: a line of status text is what made a failed
					// install look like a successful one.
					if (!_ok)
					{
						juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
							juce::String(productName) + ": install failed", juce::String(_text));
					}
				};

				if (!_success)
				{
					report(false, _message);
					return;
				}

				// Written down before anything is reported: the record is what tells later which
				// version is on disk, whoever started the install. Unless it is still waiting for the
				// rights to be copied into place, in which case finishPending writes it down instead.
				if (!pending || pending->size() == waiting)
					m_pluginManager.recordInstall(asset, m_installer.lastInstalledFiles());

				// The files the product brings besides it, before the install counts as done: whatever
				// comes next for the same product waits for them rather than fetching them alongside.
				// They go into the data folder, which needs no rights, so a copy still waiting for them
				// does not hold them up.
				//
				// One that does not arrive is said, but the build is installed all the same, and a run of
				// formats is not stopped over a preset.
				m_pluginManager.installAdditionalFilesAsync(asset,
					[alive, report, productName, message = _message](const bool _filesOk, const std::string& _filesMessage)
				{
					if (!*alive)
						return;

					if (!_filesOk)
					{
						juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
							juce::String(productName) + ": additional files", juce::String(_filesMessage));
					}

					report(true, _filesMessage.empty() ? message : message + ", " + _filesMessage);
				});
			}, {}, _pending);
	}
}
