#pragma once

#include "networkLib/udpServer.h"

namespace bridgeServer
{
	class UdpServer : public networkLib::UdpServer
	{
	public:
		UdpServer();

		std::vector<uint8_t> validateRequest(const std::vector<uint8_t>& _request) override;
	};
}
