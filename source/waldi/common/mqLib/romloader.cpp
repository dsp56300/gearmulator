#include "romloader.h"

namespace mqLib
{
	ROM RomLoader::findROM()
	{
		// an OS update as MIDI first, then a flash image
		auto rom = findFirstValid<ROM>(".mid", 300 * 1024, 400 * 1024);
		if(rom.isValid())
			return rom;
		return findFirstValid<ROM>(".bin", 512 * 1024, 512 * 1024);
	}
}
