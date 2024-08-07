#include "rom.h"

#include <cstring>

namespace mqLib
{
	ROM::ROM(const std::string& _filename) : wLib::ROM(_filename, g_romSize)
	{
		if(getSize() < 5)
			return;

		auto* d = reinterpret_cast<const char*>(getData());

		// OS version is right at the start of the ROM, as zero-terminated ASCII
		if(strstr(d, "2.23") != d)
			clear();
	}
}
