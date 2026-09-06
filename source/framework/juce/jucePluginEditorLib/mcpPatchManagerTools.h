#pragma once

namespace mcpServer
{
	class McpServer;
}

namespace jucePluginEditorLib
{
	class Processor;

	// Registers patch manager tools on the MCP server
	void registerPatchManagerTools(mcpServer::McpServer& _server, Processor& _processor);
}
