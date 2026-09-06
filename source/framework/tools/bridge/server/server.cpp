#include "server.h"

#include <ptypes/pinet.h>

#include "bridgeLib/types.h"
#include "networkLib/logging.h"

namespace bridgeServer
{
	Server::Server(int _argc, char** _argv)
		: m_config(_argc, _argv)
		, m_plugins(m_config)
		, m_romPool(m_config)
		, m_tcpServer([this](std::unique_ptr<networkLib::TcpStream> _stream){onClientConnected(std::move(_stream));}
		, bridgeLib::g_tcpServerPort)
		, m_lastDeviceStateUpdate(std::chrono::system_clock::now())
	{
	}

	Server::~Server()
	{
		exit(true);
		m_cvWait.notify_one();
		m_clients.clear();
	}

	void Server::run()
	{
		while(!m_exit)
		{
			std::unique_lock lock(m_cvWaitMutex);
			m_cvWait.wait_for(lock, std::chrono::seconds(10));

			cleanupClients();
			doPeriodicDeviceStateUpdate();
		}
	}

	void Server::onClientConnected(std::unique_ptr<networkLib::TcpStream> _stream)
	{
		const auto s = _stream->getPtypesStream();
		const std::string name = std::string(ptypes::iptostring(s->get_ip())) + ":" + std::to_string(s->get_port());

		std::scoped_lock lock(m_mutexClients);
		m_clients.emplace_back(std::make_unique<ClientConnection>(*this, std::move(_stream), name));
	}

	void Server::onClientException(const ClientConnection&, const networkLib::NetException& _e)
	{
		m_cvWait.notify_one();
	}

	void Server::exit(const bool _exit)
	{
		m_exit = _exit;
	}

	bridgeLib::DeviceState Server::getCachedDeviceState(const bridgeLib::SessionId& _id)
	{
		const auto it = m_cachedDeviceStates.find(_id);

		if(it == m_cachedDeviceStates.end())
		{
			static bridgeLib::DeviceState s;
			return s;
		}

		auto res = std::move(it->second);
		m_cachedDeviceStates.erase(it);
		return res;
	}

	void Server::cleanupClients()
	{
		std::scoped_lock lock(m_mutexClients);

		for(auto it = m_clients.begin(); it != m_clients.end();)
		{
			const auto& c = *it;
			if(!c->isValid())
			{
				const auto& deviceState = c->getDeviceState();

				if(deviceState.isValid())
					m_cachedDeviceStates[c->getPluginDesc().sessionId] = deviceState;

				it = m_clients.erase(it);
				LOGNET(networkLib::LogLevel::Info, "Client removed, now " << m_clients.size() << " clients left");
			}
			else
			{
				++it;
			}
		}
	}

	void Server::doPeriodicDeviceStateUpdate()
	{
		std::scoped_lock lock(m_mutexClients);

		const auto now = std::chrono::system_clock::now();

		const auto diff = std::chrono::duration_cast<std::chrono::minutes>(now - m_lastDeviceStateUpdate);

		if(diff.count() < static_cast<int>(m_config.deviceStateRefreshMinutes))
			return;

		m_lastDeviceStateUpdate = now;

		for (const auto& c : m_clients)
			c->sendDeviceState(synthLib::StateTypeGlobal);
	}
}
