#pragma once

#include <mutex>
#include <set>
#include <string>
#include <chrono>

#include "udpClient.h"

#include "bridgeLib/commands.h"

namespace bridgeClient
{
	class ServerList
	{
	public:
		using Clock = std::chrono::system_clock;
		using Timestamp = Clock::time_point;

		struct Entry
		{
			std::string host;
			bridgeLib::ServerInfo serverInfo;
			bridgeLib::Error err;
			Timestamp lastSeen;

			bool operator < (const Entry& _e) const
			{
				return host < _e.host;
			}

			bool operator == (const Entry& _e) const
			{
				return serverInfo.portTcp == _e.serverInfo.portTcp && host == _e.host;
			}
		};

		ServerList(const bridgeLib::PluginDesc& _desc);

		std::set<Entry> getEntries() const;

		static void removeExpiredEntries(std::set<Entry>& _entries);

	private:
		void onServerFound(const std::string& _host, const bridgeLib::ServerInfo& _serverInfo, const bridgeLib::Error& _error);

		UdpClient m_udpClient;

		mutable std::mutex m_entriesMutex;
		std::set<Entry> m_entries;
	};
}
