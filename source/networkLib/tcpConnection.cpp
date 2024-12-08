#include "tcpConnection.h"

#include <utility>	// std::move

#include "logging.h"
#include "tcpStream.h"

#include "../ptypes/pinet.h"

#ifndef _WIN32
#	include <netinet/tcp.h>	// TCP_NODELAY
#endif

namespace networkLib
{
	TcpConnection::TcpConnection(OnConnectedFunc _onConnected) : m_onConnected(std::move(_onConnected))
	{
	}

	void TcpConnection::onConnected(ptypes::ipstream* _stream) const
	{
		constexpr int opt = 1;
		const int res = ::setsockopt(_stream->get_handle(), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&opt), sizeof(opt));
		if (res < 0)
		{
			const int err = errno;
			LOGNET(LogLevel::Error, static_cast<const char*>(ptypes::iptostring(_stream->get_myip())) << ": Failed to set socket option TCP_NODELAY, err " << err << ": " << strerror(err));
		}

		m_onConnected(std::make_unique<TcpStream>(_stream));
	}
}
