#include "tabgroup.h"

namespace genericUI
{
	TabGroup::TabGroup(std::string _name, std::vector<std::string> _pageNames, std::vector<std::string> _buttonNames)
		: m_name(std::move(_name))
		, m_pageNames(std::move(_pageNames))
		, m_buttonNames(std::move(_buttonNames))
	{
	}
}
