#include "tcpClient.h"

#include <utility>

#include "logging.h"
#include "../ptypes/pinet.h"

namespace networkLib
{
	TcpClient::TcpClient(std::string _host, const uint32_t _port, OnConnectedFunc _onConnected)
		: TcpConnection(std::move(_onConnected))
		, m_host(std::move(_host))
		, m_port(_port)
	{
		start();
	}

	void TcpClient::threadFunc()
	{
		auto* stream = new ptypes::ipstream(m_host.c_str(), static_cast<int>(m_port));

		while(!exit())
		{
			try
			{
				stream->open();

				if (stream->get_active())
				{
					onConnected(stream);
					break;
				}
			}
			catch (ptypes::exception* e)
			{
				LOGNET(LogLevel::Warning, "Network Error: " << static_cast<const char*>(e->get_message()));
				delete e;
			}
		}
	}
}
