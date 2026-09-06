#pragma once

#include "settingsPlugin.h"

namespace jucePluginEditorLib
{
	struct Skin;
}

namespace jucePluginEditorLib
{
	class Processor;

	class SettingsSkin : public SettingsPlugin
	{
	public:
		SettingsSkin(Processor& _processor);

		std::string getCategoryName() const override {return "Skin";}
		std::string getTemplateName() const override { return "tus_settings_skin"; }

		void createUi(Rml::Element* _root) override;
	private:
		void exportSkin(const Skin& _skin) const;
	};
}
