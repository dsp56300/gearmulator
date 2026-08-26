#pragma once

#include "networkThread.h"

#include <functional>
#include <memory>

namespace ptypes
{
	class ipstream;
}

namespace networkLib
{
	class TcpStream;

	class TcpConnection : protected NetworkThread
	{
	public:
		using OnConnectedFunc = std::function<void(std::unique_ptr<TcpStream>)>;

	protected:
		explicit TcpConnection(OnConnectedFunc _onConnected);
		virtual ~TcpConnection() = default;
		void onConnected(ptypes::ipstream* _stream) const;

	private:
		OnConnectedFunc m_onConnected;
	};	
}
