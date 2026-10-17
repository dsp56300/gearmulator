#pragma once

namespace mcpServer
{
	class McpServer;
}

namespace jucePluginEditorLib
{
	class Processor;

	// The part of the editor tooling that is about the plugin editor itself rather than its DOM: the
	// GUI scale, which is kept in the plugin config, and the window size. Everything else is in
	// mcpServerLib/mcpDomTools.h.
	void registerEditorTools(mcpServer::McpServer& _server, Processor& _processor);
}
