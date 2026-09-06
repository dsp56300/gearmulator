#pragma once

#include <vector>

#include "networkThread.h"

namespace networkLib
{
	class UdpServer : NetworkThread
	{
	public:
		UdpServer(const int _udpPort) : m_port(_udpPort)
		{
			start();
		}
	protected:
		void threadFunc() override;

		virtual uint32_t getMaxRequestSize() const { return 60000; }
		virtual std::vector<uint8_t> validateRequest(const std::vector<uint8_t>& _request) = 0;

	private:
		const int m_port;
	};
}
