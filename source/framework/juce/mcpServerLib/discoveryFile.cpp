#include "discoveryFile.h"

#include "juce_core/juce_core.h"

namespace mcpServer
{
	std::mutex DiscoveryFile::s_mutex;

	std::string DiscoveryFile::getDiscoveryFilePath()
	{
		const auto homeDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
		return homeDir.getChildFile(".gearmulator_mcp.json").getFullPathName().toStdString();
	}

	void DiscoveryFile::registerInstance(const DiscoveryEntry& _entry)
	{
		std::lock_guard lock(s_mutex);

		auto instances = readInstances();

		// Remove any stale entry on the same port
		instances.erase(std::remove_if(instances.begin(), instances.end(),
			[&](const DiscoveryEntry& e) { return e.port == _entry.port; }), instances.end());

		instances.push_back(_entry);

		// Write
		auto arr = JsonValue::array();
		for (const auto& inst : instances)
		{
			auto obj = JsonValue::object();
			obj.set("pluginName", JsonValue::fromString(inst.pluginName));
			obj.set("plugin4CC", JsonValue::fromString(inst.plugin4CC));
			obj.set("port", JsonValue::fromInt(inst.port));
			obj.set("pid", JsonValue::fromInt(inst.pid));
			arr.append(obj);
		}

		const auto path = getDiscoveryFilePath();
		juce::File(path).replaceWithText(juce::JSON::toString(arr.getVar(), true));
	}

	void DiscoveryFile::unregisterInstance(const int _port)
	{
		std::lock_guard lock(s_mutex);

		auto instances = readInstances();
		instances.erase(std::remove_if(instances.begin(), instances.end(),
			[&](const DiscoveryEntry& e) { return e.port == _port; }), instances.end());

		auto arr = JsonValue::array();
		for (const auto& inst : instances)
		{
			auto obj = JsonValue::object();
			obj.set("pluginName", JsonValue::fromString(inst.pluginName));
			obj.set("plugin4CC", JsonValue::fromString(inst.plugin4CC));
			obj.set("port", JsonValue::fromInt(inst.port));
			obj.set("pid", JsonValue::fromInt(inst.pid));
			arr.append(obj);
		}

		const auto path = getDiscoveryFilePath();
		juce::File(path).replaceWithText(juce::JSON::toString(arr.getVar(), true));
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
				result.push_back(entry);
			}
		}

		return result;
	}
}
