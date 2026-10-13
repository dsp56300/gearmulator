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
		}
	protected:
		// The thread calls validateRequest(), so the derived class starts it at the end of its constructor and
		// stops it in its destructor. Started and stopped here, it would run while the derived part does not exist.
		using NetworkThread::start;
		using NetworkThread::stop;

		void threadFunc() override;

		virtual uint32_t getMaxRequestSize() const { return 60000; }
		virtual std::vector<uint8_t> validateRequest(const std::vector<uint8_t>& _request) = 0;

	private:
		const int m_port;
	};
}
