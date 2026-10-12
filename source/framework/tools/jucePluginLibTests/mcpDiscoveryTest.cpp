#include "jucePluginLibTests.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "mcpServerLib/discoveryFile.h"

#include "juce_core/juce_core.h"

namespace
{
	constexpr int g_processCount = 4;
	constexpr int g_entriesPerProcess = 50;
	constexpr const char* g_childArg = "--mcp-discovery-child";
	constexpr const char* g_fileVar = "GEARMULATOR_MCP_DISCOVERY_FILE";

	void setFileVar(const char* _value)
	{
#ifdef _WIN32
		_putenv_s(g_fileVar, _value);
#else
		if (*_value)
			setenv(g_fileVar, _value, 1);
		else
			unsetenv(g_fileVar);
#endif
	}
}

int mcpDiscoveryChild(const int _argc, char* _argv[])
{
	if (_argc != 3 || std::string(_argv[1]) != g_childArg)
		return -1;

	const int index = std::stoi(_argv[2]);

	// the exit code is the number of registrations that ran without the lock
	int unlocked = 0;

	for (int i = 0; i < g_entriesPerProcess; ++i)
	{
		mcpServer::DiscoveryEntry entry;
		entry.pluginName = "child" + std::to_string(index);
		entry.port = index * 1000 + i;
		if (!mcpServer::DiscoveryFile::registerInstance(entry))
			++unlocked;
	}
	return unlocked;
}

void testMcpDiscovery()
{
	std::cout << "Testing that MCP discovery entries of processes registering at once all survive..." << std::endl;

	// a file of our own, the real one belongs to the instances that are running right now
	const auto file = juce::File::createTempFile(".json");
	setFileVar(file.getFullPathName().toRawUTF8());

	const auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName();

	std::vector<std::unique_ptr<juce::ChildProcess>> children;
	for (int i = 0; i < g_processCount; ++i)
	{
		children.push_back(std::make_unique<juce::ChildProcess>());
		TEST_ASSERT(children.back()->start(juce::StringArray{exe, g_childArg, juce::String(i)}, 0));
	}

	uint32_t unlocked = 0;

	for (const auto& child : children)
	{
		TEST_ASSERT(child->waitForProcessToFinish(60000));
		unlocked += child->getExitCode();
	}

	const auto instances = mcpServer::DiscoveryFile::readInstances();

	setFileVar("");
	file.deleteFile();
	juce::File(file.getFullPathName() + ".lock").deleteFile();

	std::set<int> ports;
	for (const auto& e : instances)
		ports.insert(e.port);

	std::cout << "  " << ports.size() << " of " << g_processCount * g_entriesPerProcess << " entries, "
		<< unlocked << " registrations without the lock" << std::endl;
	TEST_ASSERT(unlocked == 0);
	TEST_ASSERT(ports.size() == static_cast<size_t>(g_processCount * g_entriesPerProcess));
}
