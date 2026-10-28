#pragma once

#include "httpServer.h"
#include "jsonRpc.h"
#include "mcpTool.h"
#include "mcpTypes.h"

#include <chrono>
#include <functional>
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

		// Makes every tool handler that waits give up, closes the client connections and joins their threads.
		// When it returns no handler runs any more and none will start, nor will anything queued for the
		// message thread. Safe to call from any thread, the message thread included, and more than once
		void stop();
		bool isRunning() const;

		int getPort() const { return m_port; }

		// Server info override
		void setServerName(const std::string& _name) { m_serverName = _name; }
		void setServerVersion(const std::string& _version) { m_serverVersion = _version; }

		// For tool handlers. Runs _func on the JUCE message thread and returns its result, or runs it right
		// here if this is the message thread or there is none. Throws if the server stops before _func is
		// done. If the server is stopped from inside _func this returns while _func still runs, so _func
		// must capture by value, never the caller's locals by reference
		JsonValue runOnMessageThread(std::function<JsonValue()> _func);

		// For tool handlers that wait. Returns true early once the server is stopping, false after _timeout
		bool waitForStop(std::chrono::milliseconds _timeout);
		bool isStopping() const;

	private:
		struct StopState;

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

		// Shared with calls queued for the message thread, which can outlive the server. Replaced by start()
		std::shared_ptr<StopState> m_stopState;

		std::unique_ptr<HttpServer> m_httpServer;

		std::mutex m_toolsMutex;
		std::vector<ToolDef> m_tools;

		std::mutex m_sseMutex;
		std::vector<networkLib::Stream*> m_sseClients;
	};
}
