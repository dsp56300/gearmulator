#pragma once

#include <cstdint>

#include "networkLib/udpServer.h"

namespace bridgeServer
{
	class UdpServer : public networkLib::UdpServer
	{
	public:
		// discovery listens on the fixed UDP port the clients broadcast to, and reports the TCP port to connect to
		explicit UdpServer(uint32_t _portTcp);

		std::vector<uint8_t> validateRequest(const std::vector<uint8_t>& _request) override;

	private:
		const uint32_t m_portTcp;
	};
}
