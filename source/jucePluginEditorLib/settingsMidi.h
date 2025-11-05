#pragma once

#include "pluginEditor.h"

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
		SettingsMidi(Processor& _processor);
		~SettingsMidi() override;

		Processor& getProcessor() const { return m_processor; }

		std::string getCategoryName() const override { return "MIDI"; }
		std::string getTemplateName() const override { return "tus_settings_midi"; }

		void createUi(Rml::Element* _root) override;

	private:
		Processor& m_processor;
	};
}
