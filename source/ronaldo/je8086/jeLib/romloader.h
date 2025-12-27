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
		static std::vector<Rom> findROMs();

	private:
		using Range = std::pair<uint32_t, uint32_t>;
		using Data = std::vector<uint8_t>;
		using MidiData = std::pair<std::string, Data>;

		static Rom loadFromMidiFiles(const std::vector<MidiData>& _files);
		static void loadFromMidiFiles(std::vector<MidiData>& _filesKeyboard, std::vector<MidiData>& _filesRack, const std::vector<std::string>& _files);

		static Range parseSysexDump(std::vector<uint8_t>& _fullRom, const std::vector<uint8_t>& _sysex);
	};
}
