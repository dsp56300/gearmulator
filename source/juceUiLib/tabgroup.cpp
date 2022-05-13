#include "tabgroup.h"

#include "editor.h"

namespace genericUI
{
	TabGroup::TabGroup(std::string _name, std::vector<std::string> _pageNames, std::vector<std::string> _buttonNames)
		: m_name(std::move(_name))
		, m_pageNames(std::move(_pageNames))
		, m_buttonNames(std::move(_buttonNames))
	{
	}

	void TabGroup::create(const Editor& _editor)
	{
		for (const auto& pageName : m_pageNames)
			m_tabs.push_back(_editor.findComponent(pageName));

		for (const auto& buttonName : m_buttonNames)
			m_tabButtons.push_back(_editor.findComponentT<juce::Button>(buttonName));

		for(size_t i=0; i<m_tabButtons.size(); ++i)
			m_tabButtons[i]->onClick = [this, i] { setPage(i); };

		setPage(0);
	}

	void TabGroup::setPage(const size_t _page) const
	{
		for(size_t i=0; i<m_tabs.size(); ++i)
		{
			m_tabs[i]->setVisible(_page == i);
			m_tabButtons[i]->setToggleState(_page == i, juce::dontSendNotification);
		}
	}
}
