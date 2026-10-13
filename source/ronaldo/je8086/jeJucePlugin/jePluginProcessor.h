#pragma once

#include "jeLib/rom.h"

#include "jucePluginEditorLib/pluginProcessor.h"

namespace jeJucePlugin
{
	class AudioPluginAudioProcessor : public jucePluginEditorLib::Processor
	{
	public:
	    AudioPluginAudioProcessor();

	    jucePluginEditorLib::PluginEditorState* createEditorState() override;
	    synthLib::Device* newDevice(const synthLib::DeviceCreateParams& _params) override;
		void getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const override;

	    pluginLib::Controller* createController() override;

		// the ROM the firmware selection points to, see pluginLib::Processor::getFirmwareIndex()
		const jeLib::Rom& getSelectedRom() const;
		const auto& getRoms() const { return m_roms; }

    private:
		std::vector<jeLib::Rom> m_roms;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)
	};
}
