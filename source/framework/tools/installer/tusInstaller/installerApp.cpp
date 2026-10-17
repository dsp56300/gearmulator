#include "installerApp.h"

#include "installerDataProvider.h"
#include "installerDocument.h"
#include "mainWindow.h"

#include "pluginManagerLib/config.h"
#include "pluginManagerLib/elevatedTask.h"
#include "pluginManagerLib/selfUpdate.h"

#include "juceRmlUi/juceRmlComponent.h"

#include "pluginManagerLib/log.h"

#include "mcpServerLib/discoveryFile.h"
#include "mcpServerLib/mcpDomTools.h"
#include "mcpServerLib/mcpServer.h"

namespace pluginManager
{
	namespace
	{
		// What is read from a file dropped on the window. A login is a line; anything this size is
		// something else that was dropped by accident.
		constexpr juce::int64 g_maxDroppedFileSize = 64 * 1024;

		std::vector<std::string> parameters()
		{
			std::vector<std::string> args;

			if (auto* app = juce::JUCEApplicationBase::getInstance())
			{
				for (const auto& arg : app->getCommandLineParameterArray())
					args.push_back(arg.toStdString());
			}

			return args;
		}
	}

	InstallerApp::InstallerApp() = default;
	InstallerApp::~InstallerApp() = default;

	const juce::String InstallerApp::getApplicationName()
	{
		return g_appName;
	}

	const juce::String InstallerApp::getApplicationVersion()
	{
		return g_appVersion;
	}

	// A second copy of this program, started through UAC to write an install into a folder that
	// belongs to the system. It does that one thing and is gone again, without a window and without
	// touching anything else - see ElevatedTask.
	bool InstallerApp::moreThanOneInstanceAllowed()
	{
		// The same goes for the copy an update has just started: the one that started it is still
		// quitting, and turning this one away would leave none at all.
		return ElevatedTask::isWorkerRequest(parameters()) || SelfUpdate::isRestart(parameters());
	}

	void InstallerApp::initialise(const juce::String& _commandLine)
	{
		int exitCode = 0;

		if (ElevatedTask::runAsWorker(parameters(), exitCode))
		{
			setApplicationReturnValue(exitCode);
			quit();
			return;
		}

		// A folder of its own to keep everything in, which is what makes this testable without
		// standing on the settings and the logins of whoever is using the machine.
		const auto args = parameters();

		for (size_t i = 0; i + 1 < args.size(); ++i)
		{
			if (args[i] == "--settings")
				PlatformUtils::setSettingsFolder(args[i + 1]);
		}

		Log::start();

		// Whatever an update moved out of the way last time. This program is running, so it is by
		// definition not the copy that was put aside.
		SelfUpdate::cleanUp();

		m_pluginManager = std::make_unique<PluginManager>();

		m_dataProvider = std::make_unique<InstallerDataProvider>();
		m_document = std::make_unique<InstallerDocument>(*m_dataProvider);
		m_mainWindow = std::make_unique<MainWindow>(*m_document);

		m_mainWindow->setTextHandler([this](const std::string& _text) { onTextReceived(_text); });

		// Not on the window: the Rml component is what JUCE gives every file drag to, and it only
		// passes on the ones no element in the document took.
		m_document->getRmlComponent()->setUnhandledFilesCallback(
			[this](const std::vector<std::string>& _files) { onFilesDropped(_files); });

		m_document->setHandlers([this] { onTeamLogin(); }, [this] { onDonatorLogin(); }, [this] { refresh(); });
		m_document->setTeamLoginAvailable(m_pluginManager->isTeamLoginEnabled());
		m_document->setTeamLoggedIn(m_pluginManager->isLoggedIn(Channel::Internal, {}));

		if (_commandLine.contains("--mcp"))
			startMcpServer();

		refresh();
	}

	void InstallerApp::shutdown()
	{
		if (m_mcpServer)
		{
			mcpServer::DiscoveryFile::unregisterInstance(m_mcpServer->getPort());
			m_mcpServer.reset();
		}

		m_mainWindow.reset();
		m_document.reset();
		m_dataProvider.reset();
		m_pluginManager.reset();

		Log::stop();
	}

	void InstallerApp::onFilesDropped(const std::vector<std::string>& _files)
	{
		for (const auto& name : _files)
		{
			const juce::File file(name);

			// A login is a line of text. Reading a plugin somebody dragged in by mistake would be a
			// pointless few hundred megabytes.
			if (!file.existsAsFile() || file.getSize() > g_maxDroppedFileSize)
				continue;

			onTextReceived(file.loadFileAsString().toStdString());
		}
	}

	void InstallerApp::onTextReceived(const std::string& _text)
	{
		std::vector<SharedLogin> logins;

		// Everything dropped on the window arrives here, and so does every Ctrl+V the document did
		// not want. What is not one of ours is simply not ours, and saying so would be noise.
		if (!LoginFile::parse(_text, logins))
			return;

		// What it will do, before it does it. This is access to somebody else's folder arriving
		// from a file or a clipboard, so it is not taken on the strength of a drop.
		juce::String what;

		for (const auto& login : logins)
			what += "\n    " + juce::String(login.slug) + "  (" + juce::String(login.credentials.user) + ")";

		const auto one = logins.size() == 1;

		juce::NativeMessageBox::showYesNoBox(juce::MessageBoxIconType::QuestionIcon,
			one ? "Add this login?" : "Add these logins?",
			juce::String(one ? "This adds a donator login for:" : "This adds donator logins for:")
				+ what + "\n\nDo you want to add " + (one ? "it" : "them") + "?",
			nullptr,
			juce::ModalCallbackFunction::create([this, logins](const int _result)
			{
				if (_result)
					addSharedLogins(logins, 0, std::make_shared<std::vector<std::string>>());
			}));
	}

	// One at a time rather than all at once: each is a request to the server, and doing them in
	// order keeps the answer for one from arriving in the middle of another.
	void InstallerApp::addSharedLogins(std::vector<SharedLogin> _logins, const size_t _index,
		                                   std::shared_ptr<std::vector<std::string>> _results)
	{
		if (_index >= _logins.size())
		{
			juce::String report;

			for (const auto& line : *_results)
				report += (report.isEmpty() ? "" : ", ") + juce::String(line);

			// Once, at the end: every login that worked is a folder the catalog has not looked in yet.
			//
			// The report goes after it rather than before. Written here it lasted one statement, because
			// refresh() puts "Loading the catalog..." on the status line and the rebuild at the end of it
			// writes its own - so nothing ever said the list was being fetched again, and the report said
			// it only until the fetch came back.
			refresh([this, report] { m_document->setStatus(report.toStdString()); });
			return;
		}

		const auto slug = _logins[_index].slug;

		m_document->setStatus("Checking the login for " + slug + "...");

		// The same path the typed prompt uses, so a login from a file is checked against the server
		// before it is kept and a wrong one is refused now rather than later.
		m_pluginManager->loginAsync(Channel::Donators, slug, _logins[_index].credentials,
			[this, _logins, _index, _results, slug](const bool _success, const std::string& _error)
			{
				_results->push_back(_success ? slug + ": added" : slug + ": " + _error);

				addSharedLogins(_logins, _index + 1, _results);
			});
	}

	void InstallerApp::startMcpServer()
	{
		// A plugin may already be hosting one on the default port; start() moves on to the next free one.
		auto server = std::make_unique<mcpServer::McpServer>();

		server->setServerName(std::string("Gearmulator MCP - ") + g_appName);

		registerDomTools(*server, [this]() -> juceRmlUi::RmlComponent*
		{
			return m_document ? m_document->getRmlComponent() : nullptr;
		});

		if (!server->start())
			return;

		mcpServer::DiscoveryFile::registerThisProcess(g_appName, "TUSP", *server);

		m_mcpServer = std::move(server);
	}

	void InstallerApp::refresh(std::function<void()> _onDone)
	{
		m_document->setStatus("Loading the catalog...");
		m_document->setBusy(true);

		m_pluginManager->refreshAsync([this, onDone = std::move(_onDone)](const bool _success, const std::string& _error)
		{
			m_document->setBusy(false);

			// Handed over rather than shown: the rebuild below writes the status line, and it can
			// only say what the list means if it knows whether the list is all there is.
			m_document->setProblem(_success ? std::string() : _error);

			m_document->rebuild(*m_pluginManager);

			if (onDone)
				onDone();
		});
	}

	void InstallerApp::onTeamLogin()
	{
		// The same button logs out again.
		if (m_pluginManager->isLoggedIn(Channel::Internal, {}))
		{
			m_pluginManager->logout(Channel::Internal, {});
			m_document->setTeamLoggedIn(false);
			refresh();
			return;
		}

		m_document->openLogin("Team login", false, [this](const std::string&, const Credentials& _credentials)
		{
			m_pluginManager->loginAsync(Channel::Internal, {}, _credentials, [this](const bool _success, const std::string& _error)
			{
				if (!_success)
				{
					m_document->loginFailed(_error);
					return;
				}

				m_document->closeLogin();
				m_document->setTeamLoggedIn(true);

				refresh();
			});
		});
	}

	// A donator login is for one folder on the server, and the folder is the only thing this program
	// knows a plugin by until it has looked inside - a plugin that is not published is in no product
	// list. So the prompt asks for the folder, and the catalog picks the product up from the builds
	// that are in it. Removing a login again is done in Preferences, where they are listed.
	void InstallerApp::onDonatorLogin()
	{
		m_document->openLogin("Donator login", true, [this](const std::string& _folder, const Credentials& _credentials)
		{
			m_pluginManager->loginAsync(Channel::Donators, _folder, _credentials, [this](const bool _success, const std::string& _error)
			{
				if (!_success)
				{
					m_document->loginFailed(_error);
					return;
				}

				m_document->closeLogin();

				refresh();
			});
		});
	}
}
