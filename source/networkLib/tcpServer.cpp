#include "tcpServer.h"

#include "logging.h"

#include "../ptypes/pinet.h"

#include <stdexcept>

namespace networkLib
{
	TcpServer::TcpServer(OnConnectedFunc _onConnected, const int _tcpPort)
		: TcpConnection(std::move(_onConnected))
		, m_port(_tcpPort)
		, m_listener(std::make_unique<ptypes::ipstmserver>())
	{
		// Bind synchronously so a port collision surfaces here, in the caller
		// thread, instead of as a silent warning later from the worker thread.
		//
		// ptypes::ipstmserver::bindall only stores the address — the actual
		// socket bind happens lazily inside ipsvbase::open(). We call open()
		// explicitly to trigger the bind now. On failure ptypes throws a
		// ptypes::exception* (estream*); we translate that and any other
		// exception type into a std::runtime_error so callers (e.g.
		// McpServer::start's port-retry loop) only have to handle std::exception.
		try
		{
			m_listener->bindall(m_port);
			// poll() with a 0 timeout triggers the lazy open()/bind() in
			// ipsvbase. open() itself is protected, so this is the cheapest
			// public call that forces the actual bind() to run now.
			(void)m_listener->poll(-1, 0);
		}
		catch (ptypes::exception* e)
		{
			const std::string msg = e ? static_cast<const char*>(e->get_message()) : "unknown bind error";
			delete e;
			throw std::runtime_error("Failed to bind TCP port " + std::to_string(m_port) + ": " + msg);
		}
		catch (const std::exception&)
		{
			throw;
		}
		catch (...)
		{
			throw std::runtime_error("Failed to bind TCP port " + std::to_string(m_port) + ": unknown exception");
		}

		start();
	}

	TcpServer::~TcpServer()
	{
		// Join the worker thread BEFORE m_listener is destroyed. The base
		// NetworkThread destructor would stop() too late: by then m_listener
		// is already gone and threadFunc would dereference a dangling pointer.
		stop();
	}

	void TcpServer::threadFunc()
	{
		LOGNET(LogLevel::Info, "Waiting for incoming connections on TCP port " << m_port);

		ptypes::ipstream* stream = new ptypes::ipstream();

		while (!exit())
		{
			try
			{
				if (m_listener->serve(*stream, -1, 1000))
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
