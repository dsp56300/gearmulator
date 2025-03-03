#pragma once

#include "settingsCategories.h"
#include "settingsPage.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib
{
	class Editor;

	class Settings final : public juce::Component
	{
	public:
		Settings(Editor& _editor);
		~Settings() override;
		void paint(juce::Graphics& g) override;
		void resized() override;

	private:
		void doLayout();

		Editor& m_editor;

		SettingsCategories m_categories;
		SettingsPage m_page;
	};
}
