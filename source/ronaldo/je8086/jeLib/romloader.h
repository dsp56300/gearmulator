#pragma once

#include <vector>

#include "rom.h"

#include "synthLib/romLoader.h"
#include "synthLib/midiTypes.h"

namespace jeLib
{
	class RomLoader : synthLib::RomLoader
	{
	public:
		static Rom findROM();
		static std::vector<Rom> findROMs();
		static Rom loadROM(const std::string& _filename);

	private:
		using Range = std::pair<uint32_t, uint32_t>;
		using Data = std::vector<uint8_t>;
		using MidiData = std::pair<std::string, Data>;

		static Rom loadFromMidiFiles(const std::vector<MidiData>& _files);
		static void loadMidiFiles(std::vector<MidiData>& _keyboard, const std::vector<std::string>& _files);

		static Range parseSysexDump(std::vector<uint8_t>& _fullRom, const synthLib::SysexBuffer& _sysex);
	};
}
