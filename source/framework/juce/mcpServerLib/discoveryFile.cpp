#include "discoveryFile.h"

#include "juce_core/juce_core.h"

namespace mcpServer
{
	std::mutex DiscoveryFile::s_mutex;

	namespace
	{
		void writeInstances(const std::vector<DiscoveryEntry>& _instances)
		{
			auto arr = JsonValue::array();

			for (const auto& inst : _instances)
			{
				auto obj = JsonValue::object();
				obj.set("pluginName", JsonValue::fromString(inst.pluginName));
				obj.set("plugin4CC", JsonValue::fromString(inst.plugin4CC));
				obj.set("port", JsonValue::fromInt(inst.port));
				obj.set("pid", JsonValue::fromInt(inst.pid));
				obj.set("sessionId", JsonValue::fromString(inst.sessionId));
				arr.append(obj);
			}

			juce::File(DiscoveryFile::getDiscoveryFilePath()).replaceWithText(juce::JSON::toString(arr.getVar(), true));
		}
	}

	std::string DiscoveryFile::getDiscoveryFilePath()
	{
		// lets a test work on a file of its own instead of the one every running instance shares
		const auto path = juce::SystemStats::getEnvironmentVariable("GEARMULATOR_MCP_DISCOVERY_FILE", {});
		if (path.isNotEmpty())
			return path.toStdString();

		const auto homeDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
		return homeDir.getChildFile(".gearmulator_mcp.json").getFullPathName().toStdString();
	}

	void DiscoveryFile::registerInstance(const DiscoveryEntry& _entry)
	{
		modifyInstances([&](std::vector<DiscoveryEntry>& _instances)
		{
			// Remove any stale entry on the same port. Only the port a server actually bound is ever
			// registered, so nobody else holds it: an entry naming it is left over from a process that
			// died without unregistering.
			_instances.erase(std::remove_if(_instances.begin(), _instances.end(),
				[&](const DiscoveryEntry& e) { return e.port == _entry.port; }), _instances.end());

			_instances.push_back(_entry);
		});
	}

	void DiscoveryFile::unregisterInstance(const int _port)
	{
		modifyInstances([&](std::vector<DiscoveryEntry>& _instances)
		{
			_instances.erase(std::remove_if(_instances.begin(), _instances.end(),
				[&](const DiscoveryEntry& e) { return e.port == _port; }), _instances.end());
		});
	}

	void DiscoveryFile::modifyInstances(const std::function<void(std::vector<DiscoveryEntry>&)>& _modify)
	{
		// s_mutex orders the threads of this process, the file lock the processes: every plugin host and the
		// Plugin Manager rewrite the same file, and two of them reading it at once would each write back a
		// list without the other's entry. Released when it goes out of scope, and by the OS if we die.
		std::lock_guard lock(s_mutex);

		// A holder that hangs (stopped in a debugger) must not hang a plugin constructor: after the timeout
		// the file is written unlocked, as it was before the lock existed.
		juce::InterProcessLock fileLock("gearmulator_mcp_discovery");
		fileLock.enter(5000);

		auto instances = readInstances();
		_modify(instances);
		writeInstances(instances);
	}

	std::vector<DiscoveryEntry> DiscoveryFile::readInstances()
	{
		std::vector<DiscoveryEntry> result;

		const auto path = getDiscoveryFilePath();
		const juce::File file(path);

		if (!file.existsAsFile())
			return result;

		const auto text = file.loadFileAsString();
		const auto parsed = juce::JSON::parse(text);

		if (!parsed.isArray())
			return result;

		const auto* arr = parsed.getArray();
		for (const auto& item : *arr)
		{
			if (const auto* obj = item.getDynamicObject())
			{
				DiscoveryEntry entry;
				entry.pluginName = obj->getProperty("pluginName").toString().toStdString();
				entry.plugin4CC = obj->getProperty("plugin4CC").toString().toStdString();
				entry.port = static_cast<int>(obj->getProperty("port"));
				entry.pid = static_cast<int>(obj->getProperty("pid"));
				entry.sessionId = obj->getProperty("sessionId").toString().toStdString();
				result.push_back(entry);
			}
		}

		return result;
	}
}
