#pragma once

namespace Rml
{
	class Element;
}

namespace jucePluginEditorLib
{
	class Settings;

	class SettingsPage final
	{
	public:
		explicit SettingsPage(Settings& _settings);
		~SettingsPage();

	private:
		Settings& m_settings;
	};
}
