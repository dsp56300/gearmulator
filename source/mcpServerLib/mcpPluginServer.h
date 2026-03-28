#pragma once

#include "mcpServerLib/mcpServer.h"

#include <atomic>
#include <memory>
#include <string>

#include "synthLib/midiTypes.h"

namespace pluginLib
{
	class Controller;
	class Processor;
}

namespace mcpServer
{
	class McpPluginServer
	{
	public:
		explicit McpPluginServer(pluginLib::Processor& _processor, int _port = g_defaultPort);
		~McpPluginServer();

		bool start();
		void stop();
		bool isRunning() const;

		int getPort() const;

		McpServer& getServer() { return m_server; }

	private:
		void registerTools();

		// Tool implementations
		void registerParameterTools();
		void registerMidiTools();
		void registerStateTools();
		void registerDeviceInfoTools();

		static synthLib::MidiEventSource parseMidiSource(const JsonValue& _params);

		pluginLib::Processor& m_processor;
		McpServer m_server;
	};
}
