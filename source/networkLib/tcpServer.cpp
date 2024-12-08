#include "tcpServer.h"

#include "logging.h"

#include "../ptypes/pinet.h"

namespace ptypes
{
	class exception;
}

namespace networkLib
{
	TcpServer::TcpServer(OnConnectedFunc _onConnected, const int _tcpPort) : TcpConnection(std::move(_onConnected)), m_port(_tcpPort)
	{
		start();
	}

	void TcpServer::threadFunc()
	{
		ptypes::ipstmserver tcpListener;

		LOGNET(LogLevel::Info, "Waiting for incoming connections on TCP port " << m_port);

		tcpListener.bindall(m_port);

		ptypes::ipstream* stream = new ptypes::ipstream();

		while (!exit())
		{
			try
			{
				if (tcpListener.serve(*stream, -1, 1000))
				{
					LOGNET(LogLevel::Info, "Client " << static_cast<const char*>(ptypes::iptostring(stream->get_ip())) << ":" << stream->get_port() << " connected");
					onConnected(stream);
					stream = new ptypes::ipstream();
				}
			}
			catch (ptypes::exception* e)
			{
				LOGNET(LogLevel::Warning, "Network Error: " << static_cast<const char*>(e->get_message()));
				delete e;
			}
		}

		LOGNET(LogLevel::Info, "TCP server shutdown");
		delete stream;
		stream = nullptr;
	}
}
