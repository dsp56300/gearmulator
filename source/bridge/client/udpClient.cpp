#include "udpClient.h"

#include <utility>

#include "bridgeLib/commandWriter.h"
#include "bridgeLib/error.h"
#include "bridgeLib/types.h"

namespace bridgeClient
{
	UdpClient::UdpClient(const bridgeLib::PluginDesc& _desc, ServerFoundCallback&& _callback)
		: networkLib::UdpClient(bridgeLib::g_udpServerPort)
		, m_callback(std::move(_callback))
	{
		bridgeLib::PluginDesc desc = _desc;

		desc.protocolVersion = bridgeLib::g_protocolVersion;

		bridgeLib::CommandWriter w;
		desc.write(w.build(bridgeLib::Command::PluginInfo));

		baseLib::BinaryStream bs;
		w.write(bs);
		bs.toVector(m_requestPacket);

		start();
	}

	bool UdpClient::validateResponse(const std::string& _host, const std::vector<uint8_t>& _message)
	{
		bool ok = false;

		bridgeLib::CommandReader reader([&](const bridgeLib::Command _command, baseLib::BinaryStream& _binaryStream)
		{
			if(_command == bridgeLib::Command::ServerInfo)
			{
				bridgeLib::ServerInfo si;
				si.read(_binaryStream);

				if(si.protocolVersion == bridgeLib::g_protocolVersion && si.portTcp > 0)
				{
					ok = true;
					m_callback(_host, si, {});
				}
				else
				{
					bridgeLib::Error e;
					e.code = bridgeLib::ErrorCode::WrongProtocolVersion;
					e.msg =  "Wrong protocol version";
					m_callback(_host, si, e);
				}
			}
			else if(_command == bridgeLib::Command::Error)
			{
				bridgeLib::Error e;
				e.read(_binaryStream);
				m_callback(_host, {}, e);
			}
		});

		baseLib::BinaryStream bs(_message);
		reader.read(bs);
		return false;	// continue
	}
}
