#pragma once

#include "networkThread.h"

#include <string>
#include <cstdint>
#include <vector>

namespace networkLib
{
	class UdpClient : protected NetworkThread
	{
	public:
		UdpClient(int _udpPort);
		virtual ~UdpClient() = default;

		static void getBroadcastAddresses(std::vector<uint32_t>& _addresses);

	protected:
		void threadLoopFunc() override;

		virtual const std::vector<uint8_t>& getRequestPacket() = 0;
		virtual uint32_t getMaxUdpResponseSize() { return 60000; }
		virtual bool validateResponse(const std::string& _host, const std::vector<uint8_t>& _message) = 0;

	private:
		const int m_port;
		std::vector<uint32_t> m_broadcastAddresses;
	};
}
