#pragma once

#include "jsonHelpers.h"

#include <string>
#include <vector>
#include <mutex>

namespace mcpServer
{
	struct DiscoveryEntry
	{
		std::string pluginName;
		std::string plugin4CC;
		int port = 0;
		int pid = 0;
	};

	// Manages a JSON discovery file so MCP clients can find running instances
	class DiscoveryFile
	{
	public:
		static std::string getDiscoveryFilePath();

		static void registerInstance(const DiscoveryEntry& _entry);
		static void unregisterInstance(int _port);
		static std::vector<DiscoveryEntry> readInstances();

	private:
		static std::mutex s_mutex;
	};
}
