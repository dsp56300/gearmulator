#pragma once

#include "pluginEditor.h"
#include "settingsPlugin.h"

namespace pluginLib
{
	class Processor;
}

namespace jucePluginEditorLib
{
	class SettingsMidi : public SettingsPlugin
	{
	public:
		SettingsMidi(pluginLib::Processor& _processor);
		~SettingsMidi() override;

		std::string getCategoryName() const override { return "MIDI"; }

		void resized() override;

	private:
		pluginLib::Processor& m_processor;

		std::unique_ptr<juce::Label> m_midiInLabel = nullptr;
		std::unique_ptr<juce::ComboBox> m_midiIn = nullptr;
		std::unique_ptr<juce::Label> m_midiOutLabel = nullptr;
		std::unique_ptr<juce::ComboBox> m_midiOut = nullptr;
	};
}
