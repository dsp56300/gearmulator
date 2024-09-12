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

		static bool isValidRom(const std::vector<uint8_t>& _data);
	};
}
