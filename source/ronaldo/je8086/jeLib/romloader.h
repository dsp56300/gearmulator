#pragma once

#include "rom.h"

#include "synthLib/romLoader.h"

namespace jeLib
{
	class RomLoader : synthLib::RomLoader
	{
	public:
		static Rom findROM();
	};
}
