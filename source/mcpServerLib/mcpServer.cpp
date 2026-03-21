#include "mcpServer.h"

#include "networkLib/exception.h"
#include "networkLib/logging.h"

namespace mcpServer
{
	McpServer::McpServer(const int _port) : m_port(_port)
	{
	}

	McpServer::~McpServer()
	{
		stop();
	}

	void McpServer::registerTool(ToolDef _tool)
	{
		std::lock_guard lock(m_toolsMutex);
		m_tools.push_back(std::move(_tool));
	}

	bool McpServer::start()
	{
		if (m_httpServer)
			return false;

		// Try the configured port, and if it fails, try subsequent ports
		constexpr int maxPortAttempts = 100;
		for (int attempt = 0; attempt < maxPortAttempts; ++attempt)
		{
			try
			{
				const int port = m_port + attempt;
				m_httpServer = std::make_unique<HttpServer>(port, [this](const HttpRequest& _req, networkLib::Stream& _stream)
				{
					return handleRequest(_req, _stream);
				});
				m_port = port;
				LOGNET(networkLib::LogLevel::Info, "MCP server listening on port " << m_port);
				return true;
			}
			catch (const std::exception& e)
			{
				if (attempt == maxPortAttempts - 1)
				{
					LOGNET(networkLib::LogLevel::Error, "Failed to start MCP server after " << maxPortAttempts << " attempts: " << e.what());
					return false;
				}
			}
		}
		return false;
	}

	void McpServer::stop()
	{
		{
			std::lock_guard lock(m_sseMutex);
			for (auto* client : m_sseClients)
				client->close();
			m_sseClients.clear();
		}

		m_httpServer.reset();
		m_initialized = false;
	}

	bool McpServer::isRunning() const
	{
		return m_httpServer && m_httpServer->isRunning();
	}

	HttpResponse McpServer::handleRequest(const HttpRequest& _request, networkLib::Stream& _stream)
	{
		HttpResponse response;
		response.setCorsHeaders();

		// CORS preflight
		if (_request.isOptions())
		{
			LOGNET(networkLib::LogLevel::Debug, "CORS preflight for " << _request.path);
			response.statusCode = 204;
			response.statusText = "No Content";
			return response;
		}

		// SSE endpoint
		if (_request.isGet() && _request.path == "/sse")
		{
			LOGNET(networkLib::LogLevel::Info, "SSE connection requested");
			handleSseConnection(_stream);
			// SSE keeps connection open; return empty (won't be sent by HttpServer)
			return response;
		}

		// MCP message endpoint - accept POST to /message, /mcp, and /sse (Streamable HTTP)
		if (_request.isPost() && (_request.path == "/message" || _request.path == "/mcp" || _request.path == "/sse"))
		{
			LOGNET(networkLib::LogLevel::Info, "MCP POST to " << _request.path << ", body: " << _request.body.substr(0, 200));
			return handleMcpPost(_request);
		}

		// Health check
		if (_request.isGet() && (_request.path == "/" || _request.path == "/health"))
		{
			auto body = JsonValue::object();
			body.set("status", JsonValue::fromString("ok"));
			body.set("server", JsonValue::fromString(m_serverName));
			body.set("version", JsonValue::fromString(m_serverVersion));
			body.set("protocol", JsonValue::fromString(g_mcpProtocolVersion));
			response.setJsonBody(body.toJsonString());
			return response;
		}

		LOGNET(networkLib::LogLevel::Warning, "Unhandled request: " << _request.method << " " << _request.path);
		response.statusCode = 404;
		response.statusText = "Not Found";
		auto err = JsonValue::object();
		err.set("error", JsonValue::fromString("Not found"));
		response.setJsonBody(err.toJsonString());
		return response;
	}

	HttpResponse McpServer::handleMcpPost(const HttpRequest& _request)
	{
		HttpResponse response;
		response.setCorsHeaders();

		auto rpcRequest = parseJsonRpcRequest(_request.body);
		if (!rpcRequest)
		{
			LOGNET(networkLib::LogLevel::Warning, "Failed to parse JSON-RPC request: " << _request.body.substr(0, 200));
			auto rpcResp = JsonRpcResponse::error(JsonValue::null(), ErrorCode::ParseError, "Invalid JSON-RPC request");
			response.setJsonBody(serializeJsonRpcResponse(rpcResp));
			return response;
		}

		LOGNET(networkLib::LogLevel::Info, "MCP method: " << rpcRequest->method << " (id: " << rpcRequest->id.toJsonString() << ")");

		JsonRpcResponse rpcResponse;

		if (rpcRequest->method == "initialize")
			rpcResponse = handleInitialize(*rpcRequest);
		else if (rpcRequest->method == "ping")
			rpcResponse = handlePing(*rpcRequest);
		else if (rpcRequest->method == "tools/list")
			rpcResponse = handleToolsList(*rpcRequest);
		else if (rpcRequest->method == "tools/call")
			rpcResponse = handleToolsCall(*rpcRequest);
		else if (rpcRequest->method == "notifications/initialized")
		{
			LOGNET(networkLib::LogLevel::Info, "Client sent notifications/initialized");
			// Client notification, no response needed for notifications
			// But we send an empty success since HTTP needs a response
			rpcResponse = JsonRpcResponse::success(rpcRequest->id, JsonValue::object());
		}
		else
		{
			LOGNET(networkLib::LogLevel::Warning, "Unknown MCP method: " << rpcRequest->method);
			rpcResponse = JsonRpcResponse::error(rpcRequest->id, ErrorCode::MethodNotFound,
				"Unknown method: " + rpcRequest->method);
		}

		const auto responseBody = serializeJsonRpcResponse(rpcResponse);
		LOGNET(networkLib::LogLevel::Debug, "MCP response: " << responseBody.substr(0, 300));
		response.setJsonBody(responseBody);
		return response;
	}

	void McpServer::handleSseConnection(networkLib::Stream& _stream)
	{
		LOGNET(networkLib::LogLevel::Info, "Setting up SSE stream");

		// Send SSE headers
		HttpResponse headers;
		headers.setSseHeaders();
		headers.setCorsHeaders();
		const auto headerStr = headers.serialize();
		_stream.write(headerStr.data(), static_cast<uint32_t>(headerStr.size()));
		_stream.flush();

		// Send endpoint event so the client knows where to POST
		LOGNET(networkLib::LogLevel::Info, "Sending SSE endpoint event: /message");
		sendSseEvent(_stream, "endpoint", "/message");

		addSseClient(&_stream);

		// Keep connection alive until closed
		try
		{
			while (_stream.isValid())
			{
				std::this_thread::sleep_for(std::chrono::seconds(15));
				// Send keep-alive comment
				const std::string keepAlive = ": keepalive\n\n";
				_stream.write(keepAlive.data(), static_cast<uint32_t>(keepAlive.size()));
				_stream.flush();
				LOGNET(networkLib::LogLevel::Debug, "SSE keepalive sent");
			}
		}
		catch (const networkLib::NetException& e)
		{
			LOGNET(networkLib::LogLevel::Info, "SSE connection closed: " << e.what());
		}
		catch (const std::exception& e)
		{
			LOGNET(networkLib::LogLevel::Warning, "SSE connection error: " << e.what());
		}
		catch (...)
		{
			LOGNET(networkLib::LogLevel::Warning, "SSE connection closed with unknown exception");
		}

		removeSseClient(&_stream);
	}

	JsonRpcResponse McpServer::handleInitialize(const JsonRpcRequest& _request)
	{
		m_initialized = true;
		LOGNET(networkLib::LogLevel::Info, "MCP session initialized");

		auto result = JsonValue::object();
		result.set("protocolVersion", JsonValue::fromString(g_mcpProtocolVersion));

		auto serverInfo = JsonValue::object();
		serverInfo.set("name", JsonValue::fromString(m_serverName));
		serverInfo.set("version", JsonValue::fromString(m_serverVersion));
		result.set("serverInfo", serverInfo);

		auto capabilities = JsonValue::object();

		auto tools = JsonValue::object();
		capabilities.set("tools", tools);

		result.set("capabilities", capabilities);

		return JsonRpcResponse::success(_request.id, result);
	}

	JsonRpcResponse McpServer::handleToolsList(const JsonRpcRequest& _request)
	{
		std::lock_guard lock(m_toolsMutex);
		LOGNET(networkLib::LogLevel::Info, "tools/list: returning " << m_tools.size() << " tools");

		auto toolsArray = JsonValue::array();
		for (const auto& tool : m_tools)
			toolsArray.append(tool.toJson());

		auto result = JsonValue::object();
		result.set("tools", toolsArray);
		return JsonRpcResponse::success(_request.id, result);
	}

	JsonRpcResponse McpServer::handleToolsCall(const JsonRpcRequest& _request)
	{
		const auto toolName = _request.params.get("name").getString().toStdString();
		const auto arguments = _request.params.get("arguments");

		LOGNET(networkLib::LogLevel::Info, "Tool call: " << toolName);

		std::lock_guard lock(m_toolsMutex);

		for (const auto& tool : m_tools)
		{
			if (tool.name == toolName)
			{
				try
				{
					auto toolResult = tool.handler(arguments);
					LOGNET(networkLib::LogLevel::Info, "Tool " << toolName << " completed successfully");

					auto content = JsonValue::array();
					auto textContent = JsonValue::object();
					textContent.set("type", JsonValue::fromString("text"));
					textContent.set("text", JsonValue::fromString(toolResult.toJsonString()));
					content.append(textContent);

					auto result = JsonValue::object();
					result.set("content", content);
					return JsonRpcResponse::success(_request.id, result);
				}
				catch (const std::exception& e)
				{
					LOGNET(networkLib::LogLevel::Error, "Tool " << toolName << " failed: " << e.what());
					auto content = JsonValue::array();
					auto textContent = JsonValue::object();
					textContent.set("type", JsonValue::fromString("text"));
					textContent.set("text", JsonValue::fromString(std::string("Error: ") + e.what()));
					content.append(textContent);

					auto result = JsonValue::object();
					result.set("content", content);
					result.set("isError", JsonValue::fromBool(true));
					return JsonRpcResponse::success(_request.id, result);
				}
			}
		}

		return JsonRpcResponse::error(_request.id, ErrorCode::InvalidParams,
			"Unknown tool: " + toolName);
	}

	JsonRpcResponse McpServer::handlePing(const JsonRpcRequest& _request)
	{
		return JsonRpcResponse::success(_request.id, JsonValue::object());
	}

	void McpServer::sendSseEvent(networkLib::Stream& _stream, const std::string& _event, const std::string& _data)
	{
		std::string msg;
		if (!_event.empty())
			msg += "event: " + _event + "\n";
		msg += "data: " + _data + "\n\n";

		LOGNET(networkLib::LogLevel::Debug, "SSE send: event=" << _event << " data=" << _data.substr(0, 100));

		try
		{
			_stream.write(msg.data(), static_cast<uint32_t>(msg.size()));
			_stream.flush();
		}
		catch (const std::exception& e)
		{
			LOGNET(networkLib::LogLevel::Warning, "SSE send failed: " << e.what());
		}
		catch (...)
		{
			LOGNET(networkLib::LogLevel::Warning, "SSE send failed with unknown exception");
		}
	}

	void McpServer::broadcastSseEvent(const std::string& _event, const std::string& _data)
	{
		std::lock_guard lock(m_sseMutex);
		for (auto* client : m_sseClients)
			sendSseEvent(*client, _event, _data);
	}

	void McpServer::addSseClient(networkLib::Stream* _stream)
	{
		std::lock_guard lock(m_sseMutex);
		m_sseClients.push_back(_stream);
		LOGNET(networkLib::LogLevel::Info, "SSE client connected. Total: " << m_sseClients.size());
	}

	void McpServer::removeSseClient(networkLib::Stream* _stream)
	{
		std::lock_guard lock(m_sseMutex);
		m_sseClients.erase(
			std::remove(m_sseClients.begin(), m_sseClients.end(), _stream),
			m_sseClients.end());
		LOGNET(networkLib::LogLevel::Info, "SSE client disconnected. Total: " << m_sseClients.size());
	}
}
