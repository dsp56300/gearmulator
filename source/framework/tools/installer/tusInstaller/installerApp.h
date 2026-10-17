#pragma once

#include <memory>

#include <JuceHeader.h>

#include "pluginManagerLib/loginFile.h"
#include "pluginManagerLib/pluginManager.h"

namespace mcpServer
{
	class McpServer;
}

namespace pluginManager
{
	class InstallerDataProvider;
	class InstallerDocument;
	class MainWindow;

	class InstallerApp : public juce::JUCEApplication
	{
	public:
		InstallerApp();
		~InstallerApp() override;

		const juce::String getApplicationName() override;
		const juce::String getApplicationVersion() override;
		bool moreThanOneInstanceAllowed() override;

		void initialise(const juce::String& _commandLine) override;
		void shutdown() override;

	private:
		// _onDone runs once the catalog is back and the list has been rebuilt, so that a caller with
		// something to say can say it afterwards instead of having it written over - the rebuild
		// writes the status line itself.
		void refresh(std::function<void()> _onDone = {});

		// The same MCP server the plugins host, with the DOM tools pointed at this window, so that the
		// user interface can be driven from a client instead of by hand. Off unless --mcp is passed.
		void startMcpServer();

		void onTeamLogin();
		void onDonatorLogin();

		// A login handed over as a file to drop or a line to paste, rather than three fields to
		// type. Anything that is not one is ignored without a word: this sees every file dropped on
		// the window and every Ctrl+V nothing else wanted, and complaining about those would be
		// noise.
		void onTextReceived(const std::string& _text);
		void onFilesDropped(const std::vector<std::string>& _files);

		// One at a time, so that the server is asked once per login and the answer for each can be
		// told from the next. _results collects what to say at the end.
		void addSharedLogins(std::vector<SharedLogin> _logins, size_t _index,
		                     std::shared_ptr<std::vector<std::string>> _results);

		std::unique_ptr<mcpServer::McpServer> m_mcpServer;
		std::unique_ptr<InstallerDataProvider> m_dataProvider;
		std::unique_ptr<InstallerDocument> m_document;
		std::unique_ptr<MainWindow> m_mainWindow;

		// Built in initialise rather than held by value: it reads the settings, the logins and the
		// record of what is installed the moment it exists, and --settings has to have been acted
		// on by then.
		std::unique_ptr<PluginManager> m_pluginManager;
	};
}
