#pragma once

#include "jeLib/rom.h"

#include "baseLib/event.h"

#include "jucePluginEditorLib/pluginProcessor.h"

namespace jeJucePlugin
{
	class AudioPluginAudioProcessor : public jucePluginEditorLib::Processor
	{
	public:
	    AudioPluginAudioProcessor();
	    ~AudioPluginAudioProcessor() override;

	    jucePluginEditorLib::PluginEditorState* createEditorState() override;
	    synthLib::Device* createDevice() override;
		void getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const override;

	    pluginLib::Controller* createController() override;

		const jeLib::Rom& getSelectedRom() const;
		const auto& getRoms() const { return m_roms; }
		size_t getSelectedRomIndex() const { return m_selectedRom; }
		// Replaces the device with one running the given ROM. Returns false and keeps the previous ROM if that fails
		bool setSelectedRom(size_t _index);

		baseLib::Event<const jeLib::Rom*> evRomChanged;

    private:
		void saveChunkData(baseLib::BinaryStream& s) override;
		void loadChunkData(baseLib::ChunkReader& _cr) override;

		std::vector<jeLib::Rom> m_roms;
		size_t m_selectedRom = 0;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)
	};
}
