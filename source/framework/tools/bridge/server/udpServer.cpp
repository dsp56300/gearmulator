#include "udpServer.h"

#include "bridgeLib/commandReader.h"
#include "bridgeLib/commandWriter.h"
#include "bridgeLib/error.h"
#include "bridgeLib/types.h"

namespace bridgeServer
{
	UdpServer::UdpServer() : networkLib::UdpServer(bridgeLib::g_udpServerPort)
	{
	}

	std::vector<uint8_t> UdpServer::validateRequest(const std::vector<uint8_t>& _request)
	{
		// respond with server info if client talks the same protocol version
		bridgeLib::ErrorCode errorCode = bridgeLib::ErrorCode::UnexpectedCommand;
		std::string errorMsg;

		{
			bridgeLib::CommandReader reader([&](const bridgeLib::Command _command, baseLib::BinaryStream& _in)
			{
				if(_command != bridgeLib::Command::PluginInfo)
					return;

				bridgeLib::PluginDesc desc;
				desc.read(_in);
				if(desc.protocolVersion != bridgeLib::g_protocolVersion)
				{
					errorCode = bridgeLib::ErrorCode::WrongProtocolVersion;
					errorMsg = "Protocol version doesn't match";
				}
				else if(desc.pluginVersion == 0)
				{
					errorCode = bridgeLib::ErrorCode::WrongPluginVersion;
					errorMsg = "Invalid plugin version";
				}
				else if(desc.pluginName.empty() && desc.plugin4CC.empty())
				{
					errorCode = bridgeLib::ErrorCode::InvalidPluginDesc;
					errorMsg = "invalid plugin description";
				}
				else
				{
					errorCode = bridgeLib::ErrorCode::Ok;
				}
			});
			baseLib::BinaryStream bs(_request);
			reader.read(bs);
		}

		bridgeLib::CommandWriter w;

		if(errorCode == bridgeLib::ErrorCode::Ok)
		{
			bridgeLib::ServerInfo si;
			si.protocolVersion = bridgeLib::g_protocolVersion;
			si.portTcp = bridgeLib::g_tcpServerPort;
			si.portUdp = bridgeLib::g_udpServerPort;
			si.write(w.build(bridgeLib::Command::ServerInfo));
		}
		else
		{
			bridgeLib::Error err;
			err.code = errorCode;
			err.msg = errorMsg;
			err.write(w.build(bridgeLib::Command::Error));
		}

		baseLib::BinaryStream bs;
		w.write(bs);
		std::vector<uint8_t> buf;
		bs.toVector(buf);
		return buf;
	}
}
