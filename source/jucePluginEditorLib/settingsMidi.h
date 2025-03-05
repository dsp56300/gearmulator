#pragma once

#include "settingsPlugin.h"

namespace jucePluginEditorLib
{
	class SettingsMidi : public SettingsPlugin
	{
	public:
		SettingsMidi();

		std::string getCategoryName() const override { return "MIDI"; }

		void resized() override;

	private:
		juce::Label* m_midiInLabel = nullptr;
		juce::ComboBox* m_midiIn = nullptr;
		juce::Label* m_midiOutLabel = nullptr;
		juce::ComboBox* m_midiOut = nullptr;
	};
}
