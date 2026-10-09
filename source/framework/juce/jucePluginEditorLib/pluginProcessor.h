#pragma once

#include "jucePluginLib/processor.h"

#include <memory>
namespace mcpServer { class McpPluginServer; }

namespace jucePluginEditorLib
{
	class PluginEditorState;

	namespace patchManager
	{
		class PatchManager;
	}

	class Processor : public pluginLib::Processor
	{
	public:
		Processor(const BusesProperties& _busesProperties, const juce::PropertiesFile::Options& _configOptions, const pluginLib::Processor::Properties& _properties);
		~Processor() override;

		// The config file options of a product from before its config moved into the data folder. Only the migration
		// of that old file reads them (EMU-165), a new product passes {}
		static juce::PropertiesFile::Options legacyConfigOptions(const char* _appName);

		// TUS_PLUGIN_ENTRY calls this right after construction. createController() is virtual, so the base constructor
		// cannot create the controller
		void finishConstruction();

		juce::PropertiesFile::Options& getConfigOptions() { return m_configOptions; }
		juce::PropertiesFile& getConfig() { return m_config; }

		bool setLatencyBlocks(uint32_t _blocks) override;

		bool hasEditor() const override;
		juce::AudioProcessorEditor* createEditor() override;

		virtual PluginEditorState* createEditorState() = 0;
		void destroyEditorState();
		PluginEditorState* getEditorState() const { return m_editorState.get(); }

		// exists from construction on, whether an editor is ever opened or not
		patchManager::PatchManager* getPatchManager() const { return m_patchManager.get(); }

		void saveChunkData(baseLib::BinaryStream& s) override;
		bool loadCustomData(const std::vector<uint8_t>& _sourceBuffer) override;
		void loadChunkData(baseLib::ChunkReader& _cr) override;

		mcpServer::McpPluginServer* getMcpServer() const { return m_mcpServer.get(); }
		void setMcpServerEnabled(bool _enabled);

	protected:
		// the latency in blocks until the user picked another one
		virtual uint32_t getDefaultLatencyBlocks() { return getPlugin().getLatencyBlocks(); }

		// call from the constructor of the derived class, once the controller exists. Takes ownership
		void setPatchManager(patchManager::PatchManager* _patchManager);

		// call from the destructor of the derived class: after destroyEditorState(), before the controller goes
		void destroyPatchManager();

	private:
		// keeps the global scope of the skin variables in the config file
		void loadGlobalSkinVariables();

		void migratePatchManagerState(const std::vector<uint8_t>& _editorStateData) const;

		juce::File initConfigFile(const juce::PropertiesFile::Options& _o) const;
		void savePluginLoadPath();
		void startMcpServer();
		void stopMcpServer();

		std::unique_ptr<PluginEditorState> m_editorState;
		std::unique_ptr<patchManager::PatchManager> m_patchManager;

		juce::PropertiesFile::Options m_configOptions;
		juce::PropertiesFile m_config;
		baseLib::EventListener<std::string, pluginLib::SkinVariables::Scope> m_skinVariablesListener;

		std::vector<uint8_t> m_editorStateData;

		std::unique_ptr<mcpServer::McpPluginServer> m_mcpServer;
	};
}
