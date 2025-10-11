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
		using Range = std::pair<uint32_t, uint32_t>;

		static Rom loadFromMidiFiles(const std::vector<std::string>& _files);

		static Range parseSysexDump(std::vector<uint8_t>& _fullRom, const std::vector<uint8_t>& _sysex);
	};
}
