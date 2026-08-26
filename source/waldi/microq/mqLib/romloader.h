#pragma once
#include "rom.h"
#include "synthLib/romLoader.h"

namespace mqLib
{
	class RomLoader : synthLib::RomLoader
	{
	public:
		static ROM findROM();
	};
}
