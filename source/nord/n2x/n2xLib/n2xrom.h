#pragma once

#include "n2xromdata.h"
#include "n2xtypes.h"

namespace n2x
{
	class Rom : public RomData<g_romSize>
	{
	public:
		Rom();
		Rom(const std::string& _filename);

		static bool isValidRom(std::array<uint8_t, g_romSize>& _data);
	};
}
