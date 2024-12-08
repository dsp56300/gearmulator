#pragma once

#include "tcpConnection.h"

namespace networkLib
{
	class TcpServer : TcpConnection
	{
	public:
		TcpServer(OnConnectedFunc _onConnected, int _tcpPort);
	protected:
		void threadFunc() override;

	private:
		const int m_port;
	};
}
