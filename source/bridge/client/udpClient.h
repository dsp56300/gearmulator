#pragma once

#include "bridgeLib/commandReader.h"

#include "networkLib/udpClient.h"

namespace bridgeClient
{
	class UdpClient : networkLib::UdpClient
	{
	public:
		using ServerFoundCallback = std::function<void(const std::string&, const bridgeLib::ServerInfo&, const bridgeLib::Error&)>;

		UdpClient(const bridgeLib::PluginDesc& _desc, ServerFoundCallback&& _callback);

	private:
		bool validateResponse(const std::string& _host, const std::vector<uint8_t>& _message) override;
		const std::vector<uint8_t>& getRequestPacket() override
		{
			return m_requestPacket;
		}

		std::vector<uint8_t> m_requestPacket;
		ServerFoundCallback m_callback;
	};
}
