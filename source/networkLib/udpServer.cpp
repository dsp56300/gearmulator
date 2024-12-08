#include "udpServer.h"

#include <iosfwd>
#include <thread>
#include <vector>

#include "logging.h"

#include "ptypes/ptypes.h"
#include "ptypes/pinet.h"

namespace ptypes
{
	class exception;
}

namespace networkLib
{
	void UdpServer::threadFunc()
	{
		ptypes::ipmsgserver	udpListener;
		udpListener.bindall(m_port);

		LOGNET(LogLevel::Info, "UDP server started on port " << m_port);

		while (!exit())
		{
			std::vector<uint8_t> buffer;
			buffer.resize(getMaxRequestSize());

			try
			{
				// check if any broadcast is there and answer
				if (udpListener.poll(-1, 1000))
				{
					const auto count = udpListener.receive(reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()));
					if(!count)
						continue;
					buffer.resize(count);
					const auto response = validateRequest(buffer);
					if (!response.empty())
						udpListener.send(reinterpret_cast<const char*>(response.data()), static_cast<int>(response.size()));
				}
			}
			catch (ptypes::exception* e)
			{
				LOGNET(LogLevel::Warning, "Network exception: " << static_cast<const char*>(e->get_message()));
				delete e;
			}
		}

		LOGNET(LogLevel::Info, "UDP server terminated");
	}
}
