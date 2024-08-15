#include "romloader.h"

#include "synthLib/os.h"

namespace mqLib
{
	ROM RomLoader::findROM()
	{
		const auto midiFiles = findFiles(".mid", 300 * 1024, 400 * 1024);

		for (const auto& midiFile : midiFiles)
		{
			ROM rom(midiFile);
			if(rom.isValid())
				return rom;
		}

		const auto binFiles = findFiles(".bin", 512 * 1024, 512 * 1024);

		for (const auto& binFile : binFiles)
		{
			ROM rom(binFile);
			if(rom.isValid())
				return rom;
		}
		return {};
	}
}
