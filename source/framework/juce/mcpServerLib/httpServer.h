#pragma once

#include "httpRequest.h"
#include "httpResponse.h"

#include "networkLib/networkThread.h"
#include "networkLib/stream.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace networkLib
{
	class TcpServer;
	class TcpStream;
}

namespace mcpServer
{
	class HttpServer
	{
	public:
		using RequestHandler = std::function<HttpResponse(const HttpRequest&, networkLib::Stream&)>;

		HttpServer(int _port, RequestHandler _handler);
		~HttpServer();

		int getPort() const { return m_port; }
		bool isRunning() const;

		// Stops accepting, wakes every client thread blocked on its socket and joins them all. A handler
		// waiting on anything else must give up by itself, see McpServer::stop. Safe to call more than once
		void stop();

	private:
		struct Client
		{
			std::unique_ptr<networkLib::TcpStream> stream;
			std::thread thread;
			bool finished = false;	// guarded by m_clientsMutex
		};

		void onClientConnected(std::unique_ptr<networkLib::TcpStream> _stream);
		void handleClient(networkLib::TcpStream& _stream);

		static bool parseRequest(HttpRequest& _request, networkLib::Stream& _stream);
		static bool readLine(std::string& _line, networkLib::Stream& _stream);
		static bool sendResponse(const HttpResponse& _response, networkLib::Stream& _stream);

		const int m_port;
		RequestHandler m_handler;
		std::unique_ptr<networkLib::TcpServer> m_tcpServer;

		std::mutex m_clientsMutex;
		std::vector<std::unique_ptr<Client>> m_clients;
		bool m_stopping = false;	// guarded by m_clientsMutex
	};
}
