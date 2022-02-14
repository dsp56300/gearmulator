#pragma once

#include <map>
#include <string>
#include <vector>

#include "VirusParameterDescription.h"

namespace Virus
{
	class ParameterDescriptions
	{
	public:
		explicit ParameterDescriptions(const std::string& _jsonString);

		const std::vector<Description>& getDescriptions() const
		{
			return m_descriptions;
		}

	private:
		std::string loadJson(const std::string& _jsonString);

		std::map<std::string, ValueList> m_valueLists;
		std::vector<Description> m_descriptions;
	};
}
