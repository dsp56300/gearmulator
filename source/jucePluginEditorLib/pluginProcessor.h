#pragma once

#include "../jucePluginLib/processor.h"

namespace jucePluginEditorLib
{
	class Processor : public pluginLib::Processor
	{
	public:
		Processor(const BusesProperties& _busesProperties, const juce::PropertiesFile::Options& _configOptions);

		juce::PropertiesFile& getConfig() { return m_config; }

		bool setLatencyBlocks(uint32_t _blocks) override;
	private:
		juce::PropertiesFile m_config;
	};
}
