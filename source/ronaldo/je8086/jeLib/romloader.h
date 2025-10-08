#pragma once

#include <vector>

#include "rom.h"

#include "synthLib/romLoader.h"

namespace jeLib
{
	class RomLoader : synthLib::RomLoader
	{
	public:
		static Rom findROM();

	private:
		static Rom loadFromMidiFiles_Keyboard(std::vector<std::string>& files);

		static constexpr size_t RomSizeKeyboardMidi = 79372;
		static constexpr size_t RomCountKeyboardMidi = 8;
	};
}
