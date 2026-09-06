#include "serverList.h"

namespace bridgeClient
{
	constexpr std::chrono::seconds g_expireTime(10);

	ServerList::ServerList(const bridgeLib::PluginDesc& _desc) : m_udpClient(_desc, [this](const std::string& _host, const bridgeLib::ServerInfo& _serverInfo, const bridgeLib::Error& _error)
	{
		onServerFound(_host, _serverInfo, _error);
	})
	{
	}

	std::set<ServerList::Entry> ServerList::getEntries() const
	{
		std::set<Entry> entries;
		{
			std::scoped_lock lock(m_entriesMutex);
			entries = m_entries;
		}
		removeExpiredEntries(entries);
		return entries;
	}

	void ServerList::removeExpiredEntries(std::set<Entry>& _entries)
	{
		for(auto it = _entries.begin(); it != _entries.end();)
		{
			auto& e = *it;

			const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - e.lastSeen);

			if(elapsed >= g_expireTime)
				it = _entries.erase(it);
			else
				++it;
		}
	}

	void ServerList::onServerFound(const std::string& _host, const bridgeLib::ServerInfo& _serverInfo, const bridgeLib::Error& _error)
	{
		Entry e;
		e.serverInfo = _serverInfo;
		e.err = _error;
		e.host = _host;
		e.lastSeen = std::chrono::system_clock::now();

		std::scoped_lock lock(m_entriesMutex);
		m_entries.erase(e);
		removeExpiredEntries(m_entries);
		m_entries.insert(e);
	}
}
