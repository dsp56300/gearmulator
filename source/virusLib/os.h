#pragma once
#include <string>

namespace virusLib
{
	std::string getModulePath();
	std::string findROM(size_t _expectedSize = 524288);
}
