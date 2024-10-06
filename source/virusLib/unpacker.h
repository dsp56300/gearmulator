#pragma once

#include "deviceModel.h"

#include <vector>

#include "dsp56kEmu/types.h"
#include "utils.h"

namespace virusLib
{
	class ROMUnpacker
	{
	public:
		struct Firmware
		{
			std::vector<char> DSP;
			std::vector<std::vector<char>> Presets;

			[[nodiscard]] bool isValid() const
			{
				return !DSP.empty();
			}
		};

		static bool isValidInstaller(std::istream& _file);

		static Firmware getFirmware(std::istream& _file, DeviceModel _model);

	private:
		struct Chunk
		{
			char name[5];
			uint32_t size;
			std::vector<char> data;
		};

		static std::string getVtiFilename(DeviceModel _model);

		static std::vector<Chunk> getChunks(std::istream& _file);

		static std::vector<char> unpackFile(std::vector<Chunk>& _chunks, char _fileId);
	};

}
