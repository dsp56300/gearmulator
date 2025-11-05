#pragma once

namespace Rml
{
	class Element;
}

namespace jucePluginEditorLib
{
	class SettingsPlugin;
	class Settings;

	class SettingsCategory final
	{
	public:
		explicit SettingsCategory(Settings& _settings, SettingsPlugin* _plugin);
		~SettingsCategory();

		void setSelected(bool _selected) const;
		SettingsPlugin* getPlugin() const { return m_plugin; }

		Rml::Element* getPageRoot() const;

	private:
		SettingsPlugin* const m_plugin;
		Settings& m_settings;

		Rml::Element* m_button;
		Rml::Element* m_page;
	};
}
