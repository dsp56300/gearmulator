#pragma once

#include "settingsPlugin.h"

#include <utility>
#include <vector>

namespace juceRmlUi
{
	class ElemButton;
}

namespace jucePluginEditorLib
{
	class Processor;

	class SettingsGui : public SettingsPlugin
	{
	public:
		SettingsGui(Processor& _processor) : SettingsPlugin(_processor) {}

		std::string getCategoryName() const override {return "GUI";}
		std::string getTemplateName() const override { return "tus_settings_gui"; }

		void createUi(Rml::Element* _root) override;

	private:
		int getCurrentScale() const;
		void updateButtons() const;

		std::vector<std::pair<int,juceRmlUi::ElemButton*>> m_scaleButtons;
	};
}
