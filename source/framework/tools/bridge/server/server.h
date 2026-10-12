#pragma once

#include <chrono>
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
		const Config& getConfig() const { return m_config; }

		bridgeLib::DeviceState getCachedDeviceState(const bridgeLib::SessionId& _id);

	private:
		void cleanupClients();
		void doPeriodicDeviceStateUpdate();

		// The order matters: the servers' threads add clients, the clients' threads use everything above them. So the
		// servers start last and stop first, and the clients go before anything they use.
		Config m_config;

		Import m_plugins;
		RomPool m_romPool;

		bool m_exit = false;

		std::mutex m_cvWaitMutex;
		std::condition_variable m_cvWait;
		std::chrono::system_clock::time_point m_lastDeviceStateUpdate;

		// Not m_mutexClients: a client's thread takes it while the main thread holds m_mutexClients to remove, and
		// so join, that client
		std::mutex m_mutexCachedDeviceStates;
		std::map<bridgeLib::SessionId, bridgeLib::DeviceState> m_cachedDeviceStates;

		std::mutex m_mutexClients;
		std::list<std::unique_ptr<ClientConnection>> m_clients;

		UdpServer m_udpServer;
		networkLib::TcpServer m_tcpServer;
	};
}
