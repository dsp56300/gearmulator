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
	};

	mqParameters();

private:
	static std::vector<std::string> split(const std::string& _s, char _delim);

	std::vector<Parameter> m_parameters;
};
