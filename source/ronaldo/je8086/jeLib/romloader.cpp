#include "romloader.h"

namespace jeLib
{
	Rom RomLoader::findROM()
	{
		auto files = synthLib::RomLoader::findFiles(".bin", Rom::RomSizeKeyboard, Rom::RomSizeKeyboard);
		if (!files.empty())
			return Rom(files.front());

		files = synthLib::RomLoader::findFiles(".bin", Rom::RomSizeRack, Rom::RomSizeRack);
		if (!files.empty())
			return Rom(files.front());
		return {};
	}
}
