#include "controllerlink.h"

namespace genericUI
{
	ControllerLink::ControllerLink(std::string _source, std::string _dest, std::string _conditionButton)
		: m_sourceName(std::move(_source))
		, m_destName(std::move(_dest))
		, m_conditionButton(std::move(_conditionButton))
	{
	}
}
