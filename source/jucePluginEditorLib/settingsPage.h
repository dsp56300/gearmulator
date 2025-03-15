#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib
{
	class Settings;

	class SettingsPage final : public juce::Component
	{
	public:
		explicit SettingsPage(Settings& _settings);
		~SettingsPage() override;
		void paint(juce::Graphics& g) override;
		void resized() override;
		void setPage(Component* _page);

	private:
		Settings& m_settings;
		juce::Component* m_page = nullptr;
		std::unique_ptr<juce::Viewport> m_viewport;
	};
}
