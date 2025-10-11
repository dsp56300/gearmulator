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
		static Rom loadFromMidiFilesKeyboard(const std::vector<std::string>& _files);

		static bool parseSysexDump(std::vector<uint8_t>& _fullRom, const std::vector<uint8_t>& _sysex);
	};
}
