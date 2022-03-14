#pragma once

#include <vector>

namespace juce
{
	class Button;
	class Component;
}

namespace genericVirusUI
{
	class VirusEditor;

	class Tabs
	{
	public:
		explicit Tabs(VirusEditor& _editor);
	private:
		void setPage(size_t _page);

		VirusEditor& m_editor;

		std::vector<juce::Component*> m_tabs;
		std::vector<juce::Button*> m_tabButtons;
	};
}
