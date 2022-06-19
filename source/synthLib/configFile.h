#pragma once

#include <string>
#include <vector>

namespace synthLib
{
	class ConfigFile
	{
	public:
		explicit ConfigFile(const char* _filename);

		const std::vector<std::pair<std::string, std::string>>& getValues() const
		{
			return m_values;
		}
	private:
		std::vector<std::pair<std::string, std::string>> m_values;
	};
}
