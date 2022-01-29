#pragma once

#include <thread>
#include <vector>

#include "dsp56kEmu/types.h"

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

	using TPreset = std::array<uint8_t, 512>;

	void dumpToBin(const std::vector<dsp56k::TWord>& _data, const std::string& _filename);

	explicit ROMFile(const std::string& _path);

	bool getMulti(int _presetNumber, TPreset& _out) const;
	bool getSingle(int bank, int presetNumber, TPreset& _out) const;
	bool getPreset(uint32_t _offset, TPreset& _out) const;
	
	static std::string getSingleName(const TPreset& _preset);
	static std::string getMultiName(const TPreset& _preset);
	static std::string getPresetName(const TPreset& _preset, uint32_t _first, uint32_t _last);

	std::thread bootDSP(dsp56k::DSP& dsp, dsp56k::Peripherals56362& periph);

	bool isValid() const { return bootRom.size > 0; }

private:
	std::vector<Chunk> get_dsp_chunks() const;

	BootRom bootRom;
	std::vector<uint32_t> commandStream;

	const std::string m_path;
};
}
