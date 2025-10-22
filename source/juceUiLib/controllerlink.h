#pragma once

#include <string>

namespace genericUI
{
	class ControllerLink
	{
	public:
		ControllerLink(std::string _source = std::string(), std::string _dest = std::string(), std::string _conditionButton = std::string());

		const std::string& getSourceName() const { return m_sourceName; }
		const std::string& getDestName() const { return m_destName; }
		const std::string& getConditionButtonName() const { return m_conditionButton; }

	private:
		const std::string m_sourceName;
		const std::string m_destName;
		const std::string m_conditionButton;
	};
}
