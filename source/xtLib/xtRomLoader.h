#pragma once

#include "synthLib/romLoader.h"

#include <cstdint>

#include "xtRom.h"

namespace xt
{
	class RomLoader : public synthLib::RomLoader
	{
	public:
		enum class FileType
		{
			Unknown,
			FullRom,
			HalfRomA,
			HalfRomB,
			MidiUpdate,
		};

		struct File
		{
			FileType type = FileType::Unknown;
			std::vector<uint8_t> data;
			std::string name;
			uint32_t version = 0;

			bool operator < (const File& _f) const
			{
				if(version < _f.version)
					return true;
				if(version > _f.version)
					return false;
				return name < _f.name;
			}
			bool operator == (const File& _f) const
			{
				return version == _f.version && name == _f.name;
			}
		};

		static Rom findROM();

	private:
		static std::vector<File> findFiles(const std::string& _extension, size_t _sizeMin, size_t _sizeMax);
		static bool detectFileType(File& _file);
		static bool removeBootloader(std::vector<uint8_t>& _data);
		static bool detectVersion(File& _file);
	};
}
