#pragma once

#include <map>
#include <string>
#include <vector>

#include "VirusParameter.h"

namespace Virus
{
	class ParameterDescriptions
	{
	public:
		explicit ParameterDescriptions(const std::string& _jsonString);

	private:
		std::string loadJson(const std::string& _jsonString);

		std::map<std::string, std::vector<std::string>> m_valueList;
		std::vector<Parameter::Description> m_descriptions;
	};
}
