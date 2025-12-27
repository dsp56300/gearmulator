#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace jeLib
{
	class Patch
	{
	public:
		static std::string getName(const std::vector<uint8_t>& _dump);
	};
}
