#include "pluginProcessor.h"

namespace jucePluginEditorLib
{
	Processor::Processor(const BusesProperties& _busesProperties, const juce::PropertiesFile::Options& _configOptions)
	: pluginLib::Processor(_busesProperties)
	, m_config(_configOptions)
	{
	}

	bool Processor::setLatencyBlocks(const uint32_t _blocks)
	{
		if(!pluginLib::Processor::setLatencyBlocks(_blocks))
			return false;

		getConfig().setValue("latencyBlocks", static_cast<int>(_blocks));
		getConfig().saveIfNeeded();

		return true;
	}

	bool Processor::hasEditor() const
	{
		return true; // (change this to false if you choose to not supply an editor)
	}
}
