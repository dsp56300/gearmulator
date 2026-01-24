#pragma once

#include "settingsPlugin.h"

namespace juceRmlUi
{
	class ElemButton;
}

namespace jucePluginEditorLib
{
	class Processor;

	class SettingsDspAudio : public SettingsPlugin
	{
	public:
		SettingsDspAudio(Processor& _processor) : SettingsPlugin(_processor) {}

		std::string getCategoryName() const override {return "DSP & Audio";}
		std::string getTemplateName() const override { return "tus_settings_dspaudio"; }

		void createUi(Rml::Element* _root) override;

	private:
		uint32_t getCurrentLatency() const;
		void updateButtons() const;
		void updateClockButtons() const;

		std::vector<std::pair<uint32_t, juceRmlUi::ElemButton*>> m_latencyButtons;
		std::vector<std::pair<int, juceRmlUi::ElemButton*>> m_clockButtons;
	};
}
