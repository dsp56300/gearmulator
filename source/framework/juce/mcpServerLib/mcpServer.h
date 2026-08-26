#pragma once

#include "httpServer.h"
#include "jsonRpc.h"
#include "mcpTool.h"
#include "mcpTypes.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace networkLib
{
	class Stream;
}

namespace mcpServer
{
	class McpServer
	{
	public:
		explicit McpServer(int _port = g_defaultPort);
		~McpServer();

		// Tool registration
		void registerTool(ToolDef _tool);

		// Server lifecycle
		bool start();
		void stop();
		bool isRunning() const;

		int getPort() const { return m_port; }

		// Server info override
		void setServerName(const std::string& _name) { m_serverName = _name; }
		void setServerVersion(const std::string& _version) { m_serverVersion = _version; }

	private:
		HttpResponse handleRequest(const HttpRequest& _request, networkLib::Stream& _stream);
		HttpResponse handleMcpPost(const HttpRequest& _request);
		void handleSseConnection(networkLib::Stream& _stream);

		// MCP methods
		JsonRpcResponse handleInitialize(const JsonRpcRequest& _request);
		JsonRpcResponse handleToolsList(const JsonRpcRequest& _request);
		JsonRpcResponse handleToolsCall(const JsonRpcRequest& _request);
		JsonRpcResponse handlePing(const JsonRpcRequest& _request);

		// SSE helpers
		static void sendSseEvent(networkLib::Stream& _stream, const std::string& _event, const std::string& _data);
		void broadcastSseEvent(const std::string& _event, const std::string& _data);
		void addSseClient(networkLib::Stream* _stream);
		void removeSseClient(networkLib::Stream* _stream);

		int m_port;
		std::string m_serverName = g_mcpServerName;
		std::string m_serverVersion = g_mcpServerVersion;
		bool m_initialized = false;

		std::unique_ptr<HttpServer> m_httpServer;

		std::mutex m_toolsMutex;
		std::vector<ToolDef> m_tools;

		std::mutex m_sseMutex;
		std::vector<networkLib::Stream*> m_sseClients;
	};
}
