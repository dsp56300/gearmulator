#include "rom.h"

#include <cstring>

namespace mqLib
{
	ROM::ROM(const std::string& _filename) : wLib::ROM(_filename, g_romSize)
	{
		verifyRom();
	}

	ROM::ROM(const std::vector<uint8_t>& _data, const std::string& _name) : wLib::ROM(_name, g_romSize, _data)
	{
		verifyRom();
	}

	void ROM::verifyRom()
	{
		if(getData().size() < 5)
			return;

		auto* d = reinterpret_cast<const char*>(getData().data());

		// OS version is right at the start of the ROM, as zero-terminated ASCII
		if(strstr(d, "2.23") != d)
			clear();
	}
}
