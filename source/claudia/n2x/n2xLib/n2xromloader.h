#pragma once

#include "n2xrom.h"

#include "synthLib/romLoader.h"

namespace n2x
{
	class RomLoader : synthLib::RomLoader
	{
	public:
		static Rom findROM();
	};
}
