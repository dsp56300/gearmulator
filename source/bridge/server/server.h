#pragma once

#include <mutex>
#include <list>
#include <condition_variable>

#include "clientConnection.h"
#include "config.h"
#include "import.h"
#include "romPool.h"
#include "udpServer.h"
#include "networkLib/tcpServer.h"

namespace bridgeServer
{
	class Server
	{
	public:
		Server(int _argc, char** _argv);
		~Server();

		void run();

		void onClientConnected(std::unique_ptr<networkLib::TcpStream> _stream);
		void onClientException(const ClientConnection& _clientConnection, const networkLib::NetException& _e);

		void exit(bool _exit);

		auto& getPlugins() { return m_plugins; }
		auto& getRomPool() { return m_romPool; }

		bridgeLib::DeviceState getCachedDeviceState(const bridgeLib::SessionId& _id);

	private:
		void cleanupClients();
		void doPeriodicDeviceStateUpdate();

		Config m_config;

		Import m_plugins;
		RomPool m_romPool;

		UdpServer m_udpServer;
		networkLib::TcpServer m_tcpServer;

		std::mutex m_mutexClients;
		std::list<std::unique_ptr<ClientConnection>> m_clients;
		std::map<bridgeLib::SessionId, bridgeLib::DeviceState> m_cachedDeviceStates;

		bool m_exit = false;

		std::mutex m_cvWaitMutex;
		std::condition_variable m_cvWait;
		std::chrono::system_clock::time_point m_lastDeviceStateUpdate;
	};
}
