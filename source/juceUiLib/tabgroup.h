#pragma once

#include <string>
#include <vector>

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

		const std::string& getName() const { return m_name; }

		const auto& getButtonNames() const { return m_buttonNames; }
		const auto& getPageNames() const { return m_pageNames; }

	private:
		std::string m_name;

		std::vector<std::string> m_pageNames;
		std::vector<std::string> m_buttonNames;
	};
}
