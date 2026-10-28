#include "mcpServer.h"

#include "networkLib/exception.h"
#include "networkLib/logging.h"

#include <juce_events/juce_events.h>

#include <condition_variable>
#include <exception>
#include <stdexcept>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace mcpServer
{
	namespace
	{
		constexpr const char* g_stoppingError = "The MCP server is stopping";

		struct MessageThreadCall
		{
			enum class State
			{
				Queued,
				Running,
				Done,
				Abandoned
			};

			std::function<JsonValue()> func;
			State state = State::Queued;
			JsonValue result;
			std::exception_ptr error;
		};

		bool isMessageThread()
		{
			const auto* mm = juce::MessageManager::getInstanceWithoutCreating();
			return mm && mm->isThisTheMessageThread();
		}
	}

	// One mutex and condition variable for everything a handler can wait for: stop(), and the message thread
	// finishing a call. The guarded state also tells a queued call whether it may still run
	struct McpServer::StopState
	{
		std::mutex mutex;
		std::condition_variable cv;
		bool stopping = false;
		bool stoppingOnMessageThread = false;
	};

	McpServer::McpServer(const int _port) : m_port(_port), m_stopState(std::make_shared<StopState>())
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

		// Calls still queued from before a stop() keep the old one, which stays stopped
		m_stopState = std::make_shared<StopState>();

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
			std::lock_guard lock(m_stopState->mutex);
			m_stopState->stopping = true;
			// Seen from the message thread, a call that is running can only be the one this is nested in
			m_stopState->stoppingOnMessageThread = isMessageThread();
		}
		m_stopState->cv.notify_all();

		// Shuts the client sockets down and joins every client thread
		m_httpServer.reset();

		{
			std::lock_guard lock(m_sseMutex);
			m_sseClients.clear();
		}
		m_initialized = false;
	}

	bool McpServer::isStopping() const
	{
		std::lock_guard lock(m_stopState->mutex);
		return m_stopState->stopping;
	}

	bool McpServer::waitForStop(const std::chrono::milliseconds _timeout)
	{
		std::unique_lock lock(m_stopState->mutex);
		return m_stopState->cv.wait_for(lock, _timeout, [this] { return m_stopState->stopping; });
	}

	JsonValue McpServer::runOnMessageThread(std::function<JsonValue()> _func)
	{
		if (!juce::MessageManager::getInstanceWithoutCreating() || isMessageThread())
			return _func();

		using State = MessageThreadCall::State;

		auto stopState = m_stopState;
		auto call = std::make_shared<MessageThreadCall>();
		call->func = std::move(_func);

		if (isStopping())
			throw std::runtime_error(g_stoppingError);

		// Captures shared state only: it may run after the server and everything the call would touch are gone
		const bool posted = juce::MessageManager::callAsync([call, stopState]
		{
			{
				std::lock_guard lock(stopState->mutex);
				if (call->state != State::Queued || stopState->stopping)
				{
					call->state = State::Abandoned;
					return;
				}
				call->state = State::Running;
			}

			JsonValue result;
			std::exception_ptr error;
			try
			{
				result = call->func();
			}
			catch (...)
			{
				error = std::current_exception();
			}

			{
				std::lock_guard lock(stopState->mutex);
				call->result = std::move(result);
				call->error = error;
				call->state = State::Done;
			}
			stopState->cv.notify_all();
		});

		if (!posted)
			throw std::runtime_error("Failed to post to the message thread");

		std::unique_lock lock(stopState->mutex);
		stopState->cv.wait(lock, [&]
		{
			if (call->state == State::Done)
				return true;
			if (!stopState->stopping)
				return false;
			// A queued call is dropped. A running one is waited for, unless the message thread itself is
			// stopping the server: then it is stopping from inside this call, which cannot finish before
			// stop() has joined us
			return call->state == State::Queued || stopState->stoppingOnMessageThread;
		});

		if (call->state != State::Done)
		{
			call->state = State::Abandoned;
			throw std::runtime_error(g_stoppingError);
		}

		if (call->error)
			std::rethrow_exception(call->error);
		return call->result;
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
			// lets a client check that the process listening here is the one its discovery entry names
#ifdef _WIN32
			body.set("pid", JsonValue::fromInt(_getpid()));
#else
			body.set("pid", JsonValue::fromInt(static_cast<int>(getpid())));
#endif
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
				if (waitForStop(std::chrono::seconds(15)))
					break;
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

		// Not called under the lock: a handler may wait for the message thread for a long time
		ToolHandler handler;
		{
			std::lock_guard lock(m_toolsMutex);
			for (const auto& tool : m_tools)
			{
				if (tool.name == toolName)
				{
					handler = tool.handler;
					break;
				}
			}
		}

		if (!handler)
		{
			return JsonRpcResponse::error(_request.id, ErrorCode::InvalidParams,
				"Unknown tool: " + toolName);
		}

		// The plugin is about to be torn down, start nothing new. One that slips past this is joined by stop()
		if (isStopping())
			return JsonRpcResponse::error(_request.id, ErrorCode::InternalError, g_stoppingError);

		try
		{
			auto toolResult = handler(arguments);
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
