#include "n2xromloader.h"

namespace n2x
{
	Rom RomLoader::findROM()
	{
		const auto files = findFiles(".bin", g_romSize, g_romSize);

		if(files.empty())
			return {};

		for (const auto& file : files)
		{
			auto rom = Rom(file);
			if(rom.isValid())
				return rom;
		}
		return {};
	}
}
