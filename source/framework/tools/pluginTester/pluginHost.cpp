#include "pluginHost.h"

CommandLinePluginHost::CommandLinePluginHost()
{
	m_formatManager.addDefaultFormats();
}

CommandLinePluginHost::~CommandLinePluginHost()
{
	setProcessor(nullptr);
	m_pluginInstance.reset();
}

bool CommandLinePluginHost::loadPlugin(const PluginDescription& _plugin)
{
	String errorMessage;

	m_pluginInstance = m_formatManager.createPluginInstance(_plugin, 48000.0f, 512, errorMessage);

	if (!m_pluginInstance)
	{
		Logger::writeToLog("Failed to load plugin: " + errorMessage);
		return false;
	}

	setProcessor(m_pluginInstance.get());
	return true;
}
