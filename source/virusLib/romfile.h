#pragma once

#include <thread>
#include <vector>

#include "../dsp56300/source/dsp56kEmu/types.h"

namespace dsp56k
{
	class Peripherals56362;
	class DSP;
}

namespace virusLib
{
class ROMFile
{
public:
	struct Chunk
	{
		uint8_t chunk_id = 0;
		uint8_t size1 = 0;
		uint8_t size2 = 0;
		std::vector<uint32_t> items;
	};

	struct BootRom
	{
		uint32_t size = 0;
		uint32_t offset = 0;
		std::vector<uint32_t> data;
	};

	explicit ROMFile(const char* _path);
	std::string loadPreset(int bank, int presetNumber);
	bool getMulti(int _presetNumber, std::array<uint8_t, 256>& _out) const;
	bool getSingle(int bank, int presetNumber, std::array<uint8_t, 256>& _out) const;
	bool getPreset(uint32_t _offset, std::array<uint8_t, 256>& _out) const;
	std::thread bootDSP(dsp56k::DSP& dsp, dsp56k::Peripherals56362& periph);

	BootRom bootRom;
	std::vector<uint32_t> commandStream;

	std::vector<dsp56k::TWord> preset;


private:
	std::vector<Chunk> get_dsp_chunks() const;

	const char* const m_path;
};
}
