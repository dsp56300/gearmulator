#pragma once

#include <functional>

namespace juceRmlUi
{
	class RmlComponent;
}

namespace mcpServer
{
	class McpServer;

	// What the tools drive. It is asked for again on every call because a window may be closed and
	// opened again while the server keeps running.
	using RmlComponentAccessor = std::function<juceRmlUi::RmlComponent*()>;

	// Registers RmlUI DOM inspection and input tools on the MCP server. Anything with an RmlUi
	// document can use these - a plugin editor, and the plugin manager alike.
	void registerDomTools(McpServer& _server, RmlComponentAccessor _getComponent);
}
