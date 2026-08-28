#pragma once

#include "synthLib/midiTypes.h"

#include <string>

namespace virusLibTest
{
	synthLib::SysexBuffer loadBinaryFixture(const std::string& _filename);
	synthLib::SysexBuffer loadHexFixture(const std::string& _filename);
}
