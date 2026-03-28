#pragma once

#include <cstdint>

namespace mcpServer
{
	constexpr const char* g_mcpProtocolVersion = "2024-11-05";
	constexpr const char* g_mcpServerName = "Gearmulator MCP";
	constexpr const char* g_mcpServerVersion = "1.0.0";
	constexpr int g_defaultPort = 13710;

	enum class ErrorCode
	{
		ParseError = -32700,
		InvalidRequest = -32600,
		MethodNotFound = -32601,
		InvalidParams = -32602,
		InternalError = -32603,
	};
}
