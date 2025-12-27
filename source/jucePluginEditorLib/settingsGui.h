#pragma once

#include "settingsPlugin.h"

namespace jucePluginEditorLib
{
	class Processor;

	class SettingsGui : public SettingsPlugin
	{
	public:
		SettingsGui(Processor& processor) : SettingsPlugin() {}

		std::string getCategoryName() const override {return "GUI";}
		std::string getTemplateName() const override { return "tus_settings_gui"; }

		void createUi(Rml::Element* _root) override {}
	private:
	};
}
