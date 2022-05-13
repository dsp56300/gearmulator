#pragma once

#include <string>
#include <vector>

namespace juce
{
	class Button;
	class Component;
}

namespace genericUI
{
	class Editor;

	class TabGroup
	{
	public:
		TabGroup() = default;
		TabGroup(std::string _name, std::vector<std::string> _pageNames, std::vector<std::string> _buttonNames);

		bool isValid() const
		{
			return !m_name.empty() && !m_buttonNames.empty() && m_buttonNames.size() == m_pageNames.size();
		}

		void create(const Editor& _editor);

		const std::string& getName() const { return m_name; }

	private:
		void setPage(const size_t _page) const;

		std::string m_name;

		std::vector<std::string> m_pageNames;
		std::vector<std::string> m_buttonNames;

		std::vector<juce::Component*> m_tabs;
		std::vector<juce::Button*> m_tabButtons;
	};
}
