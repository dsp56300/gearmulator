#include "httpServer.h"

#include "networkLib/exception.h"
#include "networkLib/logging.h"
#include "networkLib/tcpServer.h"
#include "networkLib/tcpStream.h"

#include <algorithm>
#include <sstream>

namespace mcpServer
{
	HttpServer::HttpServer(const int _port, RequestHandler _handler)
		: m_port(_port)
		, m_handler(std::move(_handler))
	{
		m_tcpServer = std::make_unique<networkLib::TcpServer>([this](std::unique_ptr<networkLib::TcpStream> _stream)
		{
			onClientConnected(std::move(_stream));
		}, _port);

		LOGNET(networkLib::LogLevel::Info, "MCP HTTP server started on port " << _port);
	}

	HttpServer::~HttpServer()
	{
		m_tcpServer.reset();

		std::lock_guard lock(m_clientsMutex);
		for (auto& t : m_clientThreads)
		{
			if (t && t->joinable())
				t->join();
		}
		m_clientThreads.clear();
	}

	bool HttpServer::isRunning() const
	{
		return m_tcpServer != nullptr;
	}

	void HttpServer::onClientConnected(std::unique_ptr<networkLib::TcpStream> _stream)
	{
		auto shared = std::shared_ptr<networkLib::TcpStream>(std::move(_stream));

		std::lock_guard lock(m_clientsMutex);

		// Clean up finished threads
		m_clientThreads.erase(
			std::remove_if(m_clientThreads.begin(), m_clientThreads.end(),
				[](const std::shared_ptr<std::thread>& _t) { return false; }),
			m_clientThreads.end());

		auto thread = std::make_shared<std::thread>([this, s = shared]()
		{
			handleClient(s);
		});

		m_clientThreads.push_back(thread);
	}

	void HttpServer::handleClient(std::shared_ptr<networkLib::TcpStream> _stream)
	{
		networkLib::Stream& stream = *_stream;

		while (_stream->isValid())
		{
			try
			{
				HttpRequest request;
				if (!parseRequest(request, stream))
					break;

				auto response = m_handler(request, stream);

				// For SSE, response headers are sent by the handler itself via the stream
				if (request.getHeader("accept") == "text/event-stream")
					continue;

				if (!sendResponse(response, stream))
					break;
			}
			catch (const networkLib::NetException&)
			{
				break;
			}
			catch (const std::exception& e)
			{
				LOGNET(networkLib::LogLevel::Error, "MCP client handler error: " << e.what());
				break;
			}
		}
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
