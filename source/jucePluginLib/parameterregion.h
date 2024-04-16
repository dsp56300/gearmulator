#pragma once

#include <string>
#include <unordered_map>

#include "parameterdescription.h"

namespace pluginLib
{
	class ParameterRegion
	{
	public:
		ParameterRegion(std::string _id, std::string _name, std::unordered_map<std::string, const Description*>&& _params);

		const auto& getId() const { return m_id; }
		const auto& getName() const { return m_name; }
		const auto& getParams() const { return m_params; }

		bool containsParameter(const std::string& _name) const
		{
			return m_params.find(_name) != m_params.end();
		}

	private:
		const std::string m_id;
		const std::string m_name;
		const std::unordered_map<std::string, const Description*> m_params;
	};
}
