#pragma once

#include <map>
#include <string>
#include <vector>

#include "parameterdescription.h"

namespace pluginLib
{
	class ParameterDescriptions
	{
	public:
		explicit ParameterDescriptions(const std::string& _jsonString);

		const std::vector<Description>& getDescriptions() const
		{
			return m_descriptions;
		}

		static std::string removeComments(std::string _json);

	private:
		std::string loadJson(const std::string& _jsonString);

		std::map<std::string, ValueList> m_valueLists;
		std::vector<Description> m_descriptions;
	};
}
