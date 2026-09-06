#pragma once

#include "JuceHeader.h"

class CommandLinePluginHost final : public AudioProcessorPlayer
{
public:
    CommandLinePluginHost();

    CommandLinePluginHost(const CommandLinePluginHost&) = delete;
	CommandLinePluginHost(CommandLinePluginHost&&) = delete;

	~CommandLinePluginHost() override;

	CommandLinePluginHost& operator=(const CommandLinePluginHost&) = delete;
	CommandLinePluginHost& operator=(CommandLinePluginHost&&) = delete;

	bool loadPlugin(const PluginDescription& _plugin);
    const AudioPluginFormatManager& getFormatManager() { return m_formatManager; }

private:
    AudioPluginFormatManager m_formatManager;
	std::unique_ptr<AudioPluginInstance> m_pluginInstance;
};
