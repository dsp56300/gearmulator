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

		void setPage(Rml::Element* _page);

	private:
		Settings& m_settings;
	};
}
