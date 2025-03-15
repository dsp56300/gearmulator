#pragma once

#include "pluginEditor.h"
#include "settingsControls.h"
#include "settingsPlugin.h"

namespace pluginLib
{
	class Processor;
}

namespace jucePluginEditorLib
{
	class SettingsMidiMatrix;

	class SettingsMidi : public SettingsPlugin
	{
	public:
		SettingsMidi(pluginLib::Processor& _processor);
		~SettingsMidi() override;

		pluginLib::Processor& getProcessor() const { return m_processor; }

		std::string getCategoryName() const override { return "MIDI"; }

		void resized() override;

	private:
		pluginLib::Processor& m_processor;

		std::unique_ptr<SettingsHeadline> m_headerPorts = nullptr;

		std::unique_ptr<juce::Label> m_midiInLabel = nullptr;
		std::unique_ptr<juce::ComboBox> m_midiIn = nullptr;
		std::unique_ptr<juce::Label> m_midiOutLabel = nullptr;
		std::unique_ptr<juce::ComboBox> m_midiOut = nullptr;

		std::unique_ptr<SettingsHeadline> m_headerRouting = nullptr;

		std::vector<std::unique_ptr<SettingsMidiMatrix>> m_matrices;
	};
}
