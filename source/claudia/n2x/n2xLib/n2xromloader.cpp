#include "n2xromloader.h"

namespace n2x
{
	Rom RomLoader::findROM()
	{
		return findFirstValid<Rom>(".bin", g_romSize, g_romSize);
	}
}
