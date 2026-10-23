#pragma once

#include <memory>
#include <mutex>

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
		// a copy, a reboot appends to the list
		std::vector<const jeLib::Rom*> getRoms() const;

    private:
		std::vector<std::string> findNewFirmwareImages() override;

		// Append only, each ROM in an allocation of its own: a state loaded on the host's thread uses them while a reboot
		// appends the ROMs it found
		std::vector<std::unique_ptr<const jeLib::Rom>> m_roms;
		mutable std::mutex m_romsMutex;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)
	};
}
