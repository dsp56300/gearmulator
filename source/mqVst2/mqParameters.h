#pragma once
#include <cstdint>
#include <string>
#include <vector>

class mqParameters
{
public:
	struct Parameter
	{
		uint8_t pah = 0;
		uint8_t pal = 0;
		uint8_t valueMin = 0;
		uint8_t valueMax = 0;
		std::string name;
		std::string shortName;
	};

	mqParameters();

	std::string getName(uint32_t _index) const;
	std::string getShortName(uint32_t _index) const;

	size_t size() const { return m_parameters.size(); }

private:
	static std::string createShortName(std::string name);
	static std::vector<std::string> split(const std::string& _s, char _delim);

	std::vector<Parameter> m_parameters;
};
