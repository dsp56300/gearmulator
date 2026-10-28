#include "httpServer.h"

#include "networkLib/exception.h"
#include "networkLib/logging.h"
#include "networkLib/tcpServer.h"
#include "networkLib/tcpStream.h"

#include "ptypes/pinet.h"

#include <algorithm>
#include <sstream>

namespace mcpServer
{
	namespace
	{
		// Wakes a thread blocked reading or writing this socket: the call fails and the thread leaves. Not
		// TcpStream::close(), which frees ptypes buffers the client thread may be using at that moment
		void shutdownSocket(networkLib::TcpStream& _stream)
		{
			auto* s = _stream.getPtypesStream();
			if (!s)
				return;
			const int handle = s->get_handle();
			if (handle != ptypes::invhandle)
				::shutdown(handle, SHUT_RDWR);
		}
	}

	HttpServer::HttpServer(const int _port, RequestHandler _handler)
		: m_port(_port)
		, m_handler(std::move(_handler))
	{
		// loopback only: the tools can load files and end the process, nothing for other machines to reach,
		// and a loopback listener raises no firewall prompt
		m_tcpServer = std::make_unique<networkLib::TcpServer>([this](std::unique_ptr<networkLib::TcpStream> _stream)
		{
			onClientConnected(std::move(_stream));
		}, _port, true);

		LOGNET(networkLib::LogLevel::Info, "MCP HTTP server started on 127.0.0.1:" << _port);
	}

	HttpServer::~HttpServer()
	{
		stop();
	}

	void HttpServer::stop()
	{
		// A keep-alive client sits in a read until it sends the next request, so wake them all up first
		std::vector<std::unique_ptr<Client>> clients;
		{
			std::lock_guard lock(m_clientsMutex);
			m_stopping = true;
			for (const auto& c : m_clients)
			{
				if (!c->finished)
					shutdownSocket(*c->stream);
			}
			clients.swap(m_clients);
		}

		// Joins the accept thread, nothing connects after this
		m_tcpServer.reset();

		for (const auto& c : clients)
		{
			if (c->thread.joinable())
				c->thread.join();
		}
	}

	bool HttpServer::isRunning() const
	{
		return m_tcpServer != nullptr;
	}

	void HttpServer::onClientConnected(std::unique_ptr<networkLib::TcpStream> _stream)
	{
		LOGNET(networkLib::LogLevel::Info, "New TCP connection accepted");

		std::lock_guard lock(m_clientsMutex);

		// Destroying the stream closes the connection
		if (m_stopping)
			return;

		// Reap the threads of clients that have gone. They are past the lock already, so joining is quick
		for (auto it = m_clients.begin(); it != m_clients.end();)
		{
			if (!(*it)->finished)
			{
				++it;
				continue;
			}
			(*it)->thread.join();
			it = m_clients.erase(it);
		}

		m_clients.push_back(std::make_unique<Client>());
		auto* client = m_clients.back().get();
		client->stream = std::move(_stream);

		client->thread = std::thread([this, client]
		{
			handleClient(*client->stream);

			{
				std::lock_guard l(m_clientsMutex);
				client->finished = true;
			}

			// stop() leaves finished clients alone, so this does not race with its shutdown. Closing here
			// lets the peer see the disconnect now rather than when the next connection reaps the entry
			client->stream->close();
		});
	}

	void HttpServer::handleClient(networkLib::TcpStream& _stream)
	{
		networkLib::Stream& stream = _stream;

		while (_stream.isValid())
		{
			try
			{
				HttpRequest request;
				if (!parseRequest(request, stream))
				{
					LOGNET(networkLib::LogLevel::Debug, "Client disconnected (no more data)");
					break;
				}

				LOGNET(networkLib::LogLevel::Info, "HTTP " << request.method << " " << request.path
					<< " (Content-Length: " << request.getContentLength()
					<< ", Accept: " << request.getHeader("accept") << ")");

				auto response = m_handler(request, stream);

				// For SSE, response headers are sent by the handler itself via the stream
				if (request.getHeader("accept") == "text/event-stream")
					continue;

				LOGNET(networkLib::LogLevel::Info, "HTTP Response: " << response.statusCode << " " << response.statusText
					<< " (body: " << response.body.size() << " bytes)");

				if (!sendResponse(response, stream))
				{
					LOGNET(networkLib::LogLevel::Warning, "Failed to send response");
					break;
				}
			}
			catch (const networkLib::NetException& e)
			{
				LOGNET(networkLib::LogLevel::Debug, "Client connection closed: " << e.what() << " (type: " << static_cast<int>(e.type()) << ")");
				break;
			}
			catch (const std::exception& e)
			{
				LOGNET(networkLib::LogLevel::Error, "Client handler error: " << e.what());
				break;
			}
		}

		LOGNET(networkLib::LogLevel::Debug, "Client handler thread exiting");
	}

	bool HttpServer::parseRequest(HttpRequest& _request, networkLib::Stream& _stream)
	{
		// Read request line
		std::string requestLine;
		if (!readLine(requestLine, _stream))
			return false;

		std::istringstream lineStream(requestLine);
		lineStream >> _request.method >> _request.path >> _request.httpVersion;

		if (_request.method.empty() || _request.path.empty())
			return false;

		// Read headers
		std::string headerLine;
		while (readLine(headerLine, _stream))
		{
			if (headerLine.empty())
				break;

			const auto colonPos = headerLine.find(':');
			if (colonPos == std::string::npos)
				continue;

			auto key = headerLine.substr(0, colonPos);
			auto value = headerLine.substr(colonPos + 1);

			// Trim leading space from value
			if (!value.empty() && value[0] == ' ')
				value = value.substr(1);

			// Lowercase key for case-insensitive lookup
			std::transform(key.begin(), key.end(), key.begin(), ::tolower);
			_request.headers[key] = value;
		}

		// Read body if content-length present
		const int contentLength = _request.getContentLength();
		if (contentLength > 0)
		{
			_request.body.resize(contentLength);
			if (!_stream.read(_request.body.data(), contentLength))
				return false;
		}

		return true;
	}

	bool HttpServer::readLine(std::string& _line, networkLib::Stream& _stream)
	{
		_line.clear();
		char c;
		while (_stream.read(&c, 1))
		{
			if (c == '\r')
			{
				// Consume \n
				_stream.read(&c, 1);
				return true;
			}
			if (c == '\n')
				return true;
			_line += c;
		}
		return false;
	}

	bool HttpServer::sendResponse(const HttpResponse& _response, networkLib::Stream& _stream)
	{
		const auto data = _response.serialize();
		try
		{
			if (!_stream.write(data.data(), static_cast<uint32_t>(data.size())))
				return false;
			return _stream.flush();
		}
		catch (...)
		{
			return false;
		}
	}
}
