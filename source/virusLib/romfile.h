#pragma once

#include <thread>
#include <vector>
#include <string>

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

	enum class Model
	{
		Invalid = -1,
		ABC,
		Snow,
		TI
	};

	using TPreset = std::array<uint8_t, 512>;

	explicit ROMFile(const std::string& _path, Model _model = Model::ABC);
	explicit ROMFile(std::vector<uint8_t> _data);

	bool getMulti(int _presetNumber, TPreset& _out) const;
	bool getSingle(int _bank, int _presetNumber, TPreset& _out) const;
	bool getPreset(uint32_t _offset, TPreset& _out) const;

	static std::string getSingleName(const TPreset& _preset);
	static std::string getMultiName(const TPreset& _preset);
	static std::string getPresetName(const TPreset& _preset, uint32_t _first, uint32_t _last);

	std::thread bootDSP(dsp56k::DSP& dsp, dsp56k::Peripherals56362& periph) const;

	bool isValid() const { return bootRom.size > 0; }

	Model getModel() const { return m_model; }

	uint32_t getSamplerate() const
	{
		return 12000000 / 256;
	}

	static uint32_t getMultiPresetSize()
	{
		return 256;
	}

	static uint32_t getSinglePresetSize()
	{
		return 256;
	}

	static uint8_t getSinglesPerBank()
	{
		return 128;
	}

	static constexpr uint32_t getRomSizeModelABC()
	{
		return 1024 * 512;
	}

	uint8_t getPresetsPerBank() const
	{
		return 128;
	}

	static std::string findROM();

	static bool loadROMData(std::string& _loadedFile, std::vector<uint8_t>& _loadedData, size_t _expectedSizeMin, size_t _expectedSizeMax);

	const std::vector<uint8_t>& getDemoData() const { return m_demoData; }

	std::string getFilename() const { return isValid() ? m_romFileName : std::string(); }

private:
	bool initialize();
	std::vector<Chunk> readChunks(std::istream& _file);

	BootRom bootRom;
	std::vector<uint32_t> m_commandStream;

	Model m_model = Model::Invalid;

	std::vector<TPreset> m_singles;
	std::vector<TPreset> m_multis;
	std::vector<uint8_t> m_demoData;

	std::string m_romFileName;
	std::vector<uint8_t> m_romFileData;
};

}
