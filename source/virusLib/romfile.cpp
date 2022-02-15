#include <cassert>
#include <fstream>
#include <algorithm>

#include "romfile.h"
#include "unpacker.h"
#include "utils.h"

#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/logging.h"

#include "../synthLib/os.h"

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace virusLib
{
void ROMFile::dumpToBin(const std::vector<dsp56k::TWord>& _data, const std::string& _filename)
{
	FILE* hFile = fopen(_filename.c_str(), "wb");

	for(size_t i=0; i<_data.size(); ++i)
	{
		const auto d = _data[i];
		const auto hsb = (d >> 16) & 0xff;
		const auto msb = (d >> 8) & 0xff;
		const auto lsb = d & 0xff;
		fwrite(&hsb, 1, 1, hFile);
		fwrite(&msb, 1, 1, hFile);
		fwrite(&lsb, 1, 1, hFile);
	}
	fclose(hFile);
}

ROMFile::ROMFile(const std::string& _path) : m_file(_path)
{
	LOG("Init access virus");

	// Open file
	LOG("Loading ROM at " << m_file);
	std::ifstream file(this->m_file, std::ios::binary | std::ios::ate);
	if (!file.is_open()) {
		LOG("Failed to load ROM at '" << m_file << "'");
#ifdef _WIN32
		const std::string errorMessage = std::string("Failed to load ROM file. Make sure it is put next to the plugin and ends with .bin");
		::MessageBoxA(nullptr, errorMessage.c_str(), "ROM not found", MB_OK);
#endif
		return;
	}

	std::istream *dsp = &file;
	ROMUnpacker::Firmware fw;

	std::unique_ptr<imemstream> memStream;

	// Check if we are dealing with a TI installer file, if so, unpack it first
	if (ROMUnpacker::isValidInstaller(file))
	{
		fw = ROMUnpacker::getFirmware(file, ROMUnpacker::VirusTISnow);
		if (!fw.isValid())
		{
			LOG("Could not unpack ROM file")
			return;
		}

		// Wrap into a stream so we can pass it into readChunks
		memStream.reset(new imemstream(fw.DSP));
		dsp = memStream.get();
	}

	const auto chunks = readChunks(*dsp);
	file.close();

	if (chunks.empty())
		return;

	bootRom.size = chunks[0].items[0];
	bootRom.offset = chunks[0].items[1];
	bootRom.data = std::vector<uint32_t>(bootRom.size);

	// The first chunk contains the bootrom
	uint32_t i = 2;
	for (; i < bootRom.size + 2; i++)
	{
		bootRom.data[i-2] = chunks[0].items[i];
	}

	// The rest of the chunks is made up of the command stream
	for (size_t j = 0; j < chunks.size(); j++)
	{
		for (; i < chunks[j].items.size(); i++)
			commandStream.emplace_back(chunks[j].items[i]);
		i = 0;
	}

//	dumpToBin(bootRom.data, _path + "_bootloader.bin");
//	dumpToBin(commandStream, _path + "_commandstream.bin");

	printf("Program BootROM size = 0x%x\n", bootRom.size);
	printf("Program BootROM offset = 0x%x\n", bootRom.offset);
	printf("Program CommandStream size = 0x%x\n", static_cast<uint32_t>(commandStream.size()));

	if(m_model == ModelD)
	{
		if (!fw.Presets.empty())
		{
			for (auto& presetFile: fw.Presets)
			{
				imemstream stream(presetFile);
				loadPresetFile(stream);
			}
		}
		else
		{
			loadPresetFiles();
		}
	}
}

std::string ROMFile::findROM()
{
	const auto res = synthLib::findROM(getRomSizeModelD());
	if (!res.empty())
		return res;
	return synthLib::findROM(getRomSizeModelABC());
}

std::vector<ROMFile::Chunk> ROMFile::readChunks(std::istream& _file)
{
	_file.seekg(0, std::ios_base::end);
	const auto fileSize = _file.tellg();

	uint32_t offset = 0x70000;
	int lastChunkId = 14;

	if(fileSize == 1024 * 1024)
	{
		// D
		m_model = ModelD;
		offset = 0x70000;
		lastChunkId = 14;
	}
	else if (fileSize == 1024 * 512)
	{
		// ABC
		m_model = ModelABC;
		offset = 0x18000;
		lastChunkId = 4;
	} else {
		LOG("Invalid ROM, unexpected filesize")
		return {};
	}

	std::vector<Chunk> chunks;
	chunks.reserve(lastChunkId + 1);

	// Read all the chunks
	for (int i = 0; i <= lastChunkId; i++)
	{
		_file.seekg(offset);

		// Read buffer
		Chunk chunk;
		//file.read(reinterpret_cast<char *>(&chunk->chunk_id), 1);
		_file.read(reinterpret_cast<char*>(&chunk.chunk_id), 1);
		_file.read(reinterpret_cast<char*>(&chunk.size1), 1);
		_file.read(reinterpret_cast<char*>(&chunk.size2), 1);

		assert(chunk.chunk_id == lastChunkId - i);

		// Format uses a special kind of size where the first byte should be decreased by 1
		const uint16_t len = ((chunk.size1 - 1) << 8) | chunk.size2;

		for (uint32_t j = 0; j < len; j++)
		{
			uint8_t buf[3];
			_file.read(reinterpret_cast<char*>(buf), 3);
			chunk.items.emplace_back((buf[0] << 16) | (buf[1] << 8) | buf[2]);
		}

		chunks.emplace_back(chunk);

		offset += 0x8000;
	}

	return chunks;
}

bool ROMFile::loadPresetFiles()
{
	bool res;
	for (auto &filename: {"S.bin", "P.bin"})
	{
		std::ifstream file(filename, std::ios::binary | std::ios::ate);
		if (!file.is_open())
		{
			LOG("Failed to open preset file " << filename);
			res &= false;
			continue;
		}
		res &= loadPresetFile(file);
		file.close();
	}
	return res;
}

bool ROMFile::loadPresetFile(std::istream& _file)
{
	_file.seekg(0, std::ios_base::end);
	const auto fileSize = _file.tellg();

	uint32_t singleCount = 0;
	uint32_t multiCount = 0;
	uint32_t multiOffset = 0;

	if(fileSize == 425984)
	{
		singleCount = 512;
		multiCount = 128;
		multiOffset = 768;
	}
	else if (fileSize == 262144)
	{
		singleCount = 512;
		multiCount = 0;
	}
	else
	{
		LOG("Unknown file size " << fileSize << " for preset file");
		return false;
	}

	_file.seekg(0);

	for(uint32_t i=0; i<singleCount; ++i)
	{
		TPreset single;
		_file.read(reinterpret_cast<char*>(&single), sizeof(single));
		m_singles.emplace_back(single);

		LOG("Loaded single " << i << ", name = " << getSingleName(single));
	}

	if(multiCount)
	{
		const auto off = std::max(singleCount, multiOffset);
		_file.seekg(off);

		for (uint32_t i = 0; i < multiCount; ++i)
		{
			TPreset multi;
			_file.read(reinterpret_cast<char*>(&multi), sizeof(multi));
			m_multis.emplace_back(multi);
		}
	}

	return true;
}

std::thread ROMFile::bootDSP(dsp56k::DSP& dsp, dsp56k::Peripherals56362& periph) const
{
	// Load BootROM in DSP memory
	for (uint32_t i=0; i<bootRom.data.size(); i++)
		dsp.memory().set(dsp56k::MemArea_P, bootRom.offset + i, bootRom.data[i]);

//	dsp.memory().saveAssembly("Virus_BootROM.asm", bootRom.offset, bootRom.size, false, false, &periph);

	// Attach command stream
	std::thread feedCommandStream([&]()
	{
		periph.getHDI08().writeRX(commandStream);
	});

	// Initialize the DSP
	dsp.setPC(bootRom.offset);
	return feedCommandStream;
}

bool ROMFile::getSingle(const int _bank, const int _presetNumber, TPreset& _out) const
{
	if(m_model == ModelD)
	{
		const auto offset = _bank * getSinglesPerBank() + _presetNumber;
		if (offset >= m_singles.size())
			return false;
		_out = m_singles[offset];
		return true;
	}

	const uint32_t offset = 0x50000 + (_bank * 0x8000) + (_presetNumber * getSinglePresetSize());

	return getPreset(offset, _out);
}

bool ROMFile::getMulti(const int _presetNumber, TPreset& _out) const
{
	if(m_model == ModelD)
	{
		if (_presetNumber >= m_multis.size())
			return false;

		_out = m_multis[_presetNumber];
		return true;
	}

	return getPreset(0x48000 + (_presetNumber * getMultiPresetSize()), _out);
}

bool ROMFile::getPreset(const uint32_t _offset, TPreset& _out) const
{
	// Open file
	std::ifstream file(this->m_file, std::ios::binary | std::ios::ate);
	if(!file.is_open())
	{
		LOG("Failed to open ROM file " << m_file)
		return false;
	}
	file.seekg(_offset);
	if(file.tellg() != _offset)
		return false;
	file.read(reinterpret_cast<char *>(_out.data()), getSinglePresetSize());
	file.close();
	return true;
}

std::string ROMFile::getSingleName(const TPreset& _preset)
{
	return getPresetName(_preset, 240, 249);
}

std::string ROMFile::getMultiName(const TPreset& _preset)
{
	return getPresetName(_preset, 4, 13);
}

std::string ROMFile::getPresetName(const TPreset& _preset, const uint32_t _first, const uint32_t _last)
{
	std::string name;

	name.reserve(11);

	for (uint32_t i = _first; i <= _last; i++)
	{
		const auto c = _preset[i];
		if(c < 32 || c > 127)
			break;
		name.push_back(static_cast<char>(c));
	}

	return name;
}

}
