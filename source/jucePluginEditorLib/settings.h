#pragma once

#include "settingsCategories.h"

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

		void setSelectedCategory(const SettingsCategory* _settingsCategory) const;

		static std::unique_ptr<Settings> createFromTemplate(Editor& _editor, const std::string& _templateName, Rml::Element* _parent);

		Rml::Element* createPageButton(const std::string& _title) const;
		Rml::Element* getPageParent() const;

	private:
		void enableAdvancedOptions(bool _enable) const;

		Editor& m_editor;
		Rml::Element* m_root;

		SettingsCategories m_categories;
	};
}
