#pragma once

#include "jucePluginLib/processor.h"

namespace jucePluginEditorLib
{
	class PluginEditorState;

	class Processor : public pluginLib::Processor
	{
	public:
		Processor(const BusesProperties& _busesProperties, const juce::PropertiesFile::Options& _configOptions, const pluginLib::Processor::Properties& _properties);
		~Processor() override;

		juce::PropertiesFile::Options& getConfigOptions() { return m_configOptions; }
		juce::PropertiesFile& getConfig() { return m_config; }

		bool setLatencyBlocks(uint32_t _blocks) override;

		bool hasEditor() const override;
		juce::AudioProcessorEditor* createEditor() override;

		virtual PluginEditorState* createEditorState() = 0;
		void destroyEditorState();

		void saveChunkData(synthLib::BinaryStream& s) override;
		bool loadCustomData(const std::vector<uint8_t>& _sourceBuffer) override;
		void loadChunkData(synthLib::ChunkReader& _cr) override;

	private:
		std::unique_ptr<PluginEditorState> m_editorState;

		juce::PropertiesFile::Options m_configOptions;
		juce::PropertiesFile m_config;

		std::vector<uint8_t> m_editorStateData;
	};
}
