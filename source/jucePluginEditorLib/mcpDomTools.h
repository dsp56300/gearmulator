#pragma once

namespace mcpServer
{
	class McpServer;
}

namespace jucePluginEditorLib
{
	class Processor;

	// Registers RmlUI DOM inspection tools on the MCP server
	void registerDomTools(mcpServer::McpServer& _server, Processor& _processor);
}
