#pragma once

#include <thread>
#include <utility>
#include <vector>

#include "dsp56kEmu/types.h"
#include "utils.h"

namespace virusLib
{

	class ROMUnpacker
	{
	public:
		enum AccessVirusTIModel
		{
			VirusTI = 1,
			VirusTISnow = 2,
			VirusTI2 = 3,
		};

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

		static Firmware getFirmware(std::istream& _file, AccessVirusTIModel _model);

	private:
		struct Chunk
		{
			char name[5];
			uint32_t size;
			std::vector<char> data;
		};

		static std::string getVtiFilename(AccessVirusTIModel _model);

		static std::vector<Chunk> getChunks(std::istream& _file);

		static std::vector<char> unpackFile(std::vector<Chunk>& _chunks, char _fileId);
	};

}
