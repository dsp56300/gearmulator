#pragma once

#include "tcpConnection.h"

#include <string>

namespace networkLib
{
	class TcpClient : TcpConnection
	{
	public:
		TcpClient(std::string _host, uint32_t _port, OnConnectedFunc _onConnected);
	protected:
		void threadFunc() override;
	private:
		const std::string m_host;
		const uint32_t m_port;
	};
}
