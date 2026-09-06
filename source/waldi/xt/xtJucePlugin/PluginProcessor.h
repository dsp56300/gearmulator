#pragma once

#include "jucePluginEditorLib/pluginProcessor.h"

namespace baseLib { class BinaryStream; class ChunkReader; }

namespace xtJucePlugin
{
	class AudioPluginAudioProcessor  : public jucePluginEditorLib::Processor
	{
	public:
	    AudioPluginAudioProcessor();
	    ~AudioPluginAudioProcessor() override;

	    jucePluginEditorLib::PluginEditorState* createEditorState() override;

		synthLib::Device* createDevice() override;
		void getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const override;

	    pluginLib::Controller* createController() override;

		bool isVoiceExpansionEnabled() const { return m_voiceExpansion; }
		void setVoiceExpansion(bool _enabled);

	protected:
		void saveChunkData(baseLib::BinaryStream& s) override;
		void loadChunkData(baseLib::ChunkReader& _cr) override;
		bool loadCustomData(const std::vector<uint8_t>& _sourceBuffer) override;

	private:
		bool m_voiceExpansion = false;
		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)
	};
}
