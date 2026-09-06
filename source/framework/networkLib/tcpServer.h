#pragma once

#include <memory>

#include "tcpConnection.h"

namespace ptypes
{
	class ipstmserver;
}

namespace networkLib
{
	class TcpServer : TcpConnection
	{
	public:
		// Throws on bind failure so callers (e.g. McpServer::start's port-retry
		// loop) can detect a port collision and try the next port.
		TcpServer(OnConnectedFunc _onConnected, int _tcpPort);
		~TcpServer() override;
	protected:
		void threadFunc() override;

	private:
		const int m_port;
		std::unique_ptr<ptypes::ipstmserver> m_listener;
	};
}
