#include "parameterregion.h"

namespace pluginLib
{
	ParameterRegion::ParameterRegion(std::string _id, std::string _name, std::unordered_map<std::string, const Description*>&& _params) : m_id(std::move(_id)), m_name(std::move(_name)), m_params(std::move(_params))
	{
	}
}
