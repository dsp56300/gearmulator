#include "udpClient.h"

#include "logging.h"

#include "../ptypes/pinet.h"

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Iphlpapi.lib")
#else
#include <ifaddrs.h>
#endif

namespace networkLib
{
	UdpClient::UdpClient(const int _udpPort) : m_port(_udpPort)
	{
		getBroadcastAddresses(m_broadcastAddresses);
		if(m_broadcastAddresses.empty())
			m_broadcastAddresses.push_back(0xff'ff'ff'ff);
	}

	void UdpClient::getBroadcastAddresses(std::vector<uint32_t>& _addresses)
	{
#ifdef _WIN32
		ULONG bufferSize = 0;
		if (GetAdaptersInfo(nullptr, &bufferSize) != ERROR_BUFFER_OVERFLOW)
			return;

		std::vector<BYTE> buffer;
		buffer.resize(bufferSize, 0);

		if (GetAdaptersInfo(reinterpret_cast<IP_ADAPTER_INFO*>(buffer.data()), &bufferSize) != ERROR_SUCCESS)
			return;

		const IP_ADAPTER_INFO* adapterInfo = reinterpret_cast<IP_ADAPTER_INFO*>(buffer.data());

		for(; adapterInfo != nullptr; adapterInfo = adapterInfo->Next)
		{
			IN_ADDR addrIp;
			IN_ADDR addrMask;

			inet_pton(AF_INET, adapterInfo->IpAddressList.IpAddress.String, &addrIp);
			inet_pton(AF_INET, adapterInfo->IpAddressList.IpMask.String, &addrMask);

			const auto broadcastAddr = addrIp.S_un.S_addr | ~addrMask.S_un.S_addr;

			if(!broadcastAddr)
				continue;

			_addresses.push_back(broadcastAddr);
		}
#else
		ifaddrs* ifap = nullptr;
		if (getifaddrs(&ifap) == 0 && ifap)
		{
			struct ifaddrs * p = ifap;
			while(p)
			{
				auto toUint32 = [](sockaddr* a) -> uint32_t
				{
					if(!a || a->sa_family != AF_INET)
						return 0;
					return ((struct sockaddr_in *)a)->sin_addr.s_addr;
				};

				const auto ifaAddr  = toUint32(p->ifa_addr);
				const auto maskAddr = toUint32(p->ifa_netmask);

				if (ifaAddr > 0)
				{
					const auto mask = ifaAddr | ~maskAddr;
					if(mask)
						_addresses.push_back(mask);
				}
				p = p->ifa_next;
			}
			freeifaddrs(ifap);
		}
#endif
	}

	void UdpClient::threadLoopFunc()
	{
		try
		{
			const auto& req = getRequestPacket();

			for (const auto broadcastAddress : m_broadcastAddresses)
			{
				ptypes::ipmessage msg(ptypes::ipaddress((ptypes::ulong)broadcastAddress), m_port);
				msg.send(reinterpret_cast<const char*>(req.data()), static_cast<int>(req.size()));
				std::vector<uint8_t> buffer;
				buffer.resize(getMaxUdpResponseSize());

				// wait for answer
				while (msg.waitfor(250))
				{
					ptypes::ipaddress addr;

					const auto count = msg.receive(reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), addr);

					if(!count)
						continue;

					buffer.resize(count);

					const auto host = std::string(ptypes::iptostring(addr));

					if(validateResponse(host, buffer))
					{
						exit(true);
						break;
					}
				}

				if(exit())
					break;
			}
		}
		catch(ptypes::exception* e)  // NOLINT(misc-throw-by-value-catch-by-reference)
		{
			LOGNET(LogLevel::Warning, "Network error: " << static_cast<const char*>(e->get_message()));
		}
	}
}
