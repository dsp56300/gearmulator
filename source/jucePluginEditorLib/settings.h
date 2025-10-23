#pragma once

#include "settingsCategories.h"
#include "settingsPage.h"

namespace Rml
{
	class Element;
}

namespace jucePluginEditorLib
{
	class Editor;

	class Settings
	{
	public:
		Settings(Editor& _editor, Rml::Element* _root);
		~Settings();

		Editor& getEditor() const { return m_editor; }

		void setSelectedCategory(const SettingsCategory* _settingsCategory);

		static std::unique_ptr<Settings> createFromTemplate(Editor& _editor, const std::string& _templateName, Rml::Element* _parent);

	private:
		Editor& m_editor;
		Rml::Element* m_root;

		SettingsCategories m_categories;
		SettingsPage m_page;
	};
}
