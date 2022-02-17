#include <cassert>
#include <fstream>

#include "romfile.h"

#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/logging.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace virusLib
{
ROMFile::ROMFile(const std::string& _path) : m_path(_path)
{
	LOG("Init access virus");

	auto chunks = get_dsp_chunks();

	if(chunks.empty())
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

	printf("ROM File: %s", _path.c_str());
	printf("Program BootROM size = 0x%x\n", bootRom.size);
	printf("Program BootROM offset = 0x%x\n", bootRom.offset);
	printf("Program BootROM len = 0x%x\n", static_cast<uint32_t>(bootRom.data.size()));
	printf("Program Commands len = 0x%x\n", static_cast<uint32_t>(commandStream.size()));
}

std::vector<ROMFile::Chunk> ROMFile::get_dsp_chunks() const
{
	uint32_t offset = 0x18000;

	// Open file
	std::ifstream file(this->m_path, std::ios::binary | std::ios::ate);

	if(!file.is_open())
	{
		LOG("Failed to load ROM at '" << m_path << "'");
#ifdef _WIN32
		const std::string errorMessage = std::string("Failed to load ROM file. Make sure it is put next to the plugin and ends with .bin");
		::MessageBoxA(nullptr, errorMessage.c_str(), "ROM not found", MB_OK);
#endif
		return {};
	}

	LOG("Loading ROM at " << m_path);

	std::vector<Chunk> chunks;
	chunks.reserve(5);

	// Read all the chunks, hardcoded to 4 for convenience
	for (int i = 0; i <= 4; i++)
	{
		file.seekg(offset);

		// Read buffer
		Chunk chunk;
		//file.read(reinterpret_cast<char *>(&chunk->chunk_id), 1);
		file.read(reinterpret_cast<char*>(&chunk.chunk_id), 1);
		file.read(reinterpret_cast<char*>(&chunk.size1), 1);
		file.read(reinterpret_cast<char*>(&chunk.size2), 1);

		assert(chunk.chunk_id == 4 - i);

		// Format uses a special kind of size where the first byte should be decreased by 1
		const uint16_t len = ((chunk.size1 - 1) << 8) | chunk.size2;

		uint8_t buf[3];

		for (uint32_t j = 0; j < len; j++)
		{
			file.read(reinterpret_cast<char*>(buf), 3);
			chunk.items.emplace_back((buf[0] << 16) | (buf[1] << 8) | buf[2]);
		}

		chunks.emplace_back(chunk);

		offset += 0x8000;
	}

	file.close();

	return chunks;
}

std::thread ROMFile::bootDSP(dsp56k::DSP& dsp, dsp56k::Peripherals56362& periph)
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

bool ROMFile::getSingle(int bank, int presetNumber, TPreset& _out) const
{
	const uint32_t offset = 0x50000 + (bank * 0x8000) + (presetNumber * 0x100);

	if(!getPreset(offset, _out))
		return false;

	std::stringstream ss;
	ss << "Loading Single: Bank " << static_cast<char>('A' + bank) << " " << std::setfill('0') << std::setw(3) << presetNumber << " [" << getSingleName(_out) << "]";

	const std::string msg(ss.str());
	
	LOG(msg);
	puts(msg.c_str());

	return true;
}

bool ROMFile::getMulti(const int _presetNumber, TPreset& _out) const
{
	// Open file
	return getPreset(0x48000 + (_presetNumber * 256), _out);
}

bool ROMFile::getPreset(const uint32_t _offset, TPreset& _out) const
{
	// Open file
	std::ifstream file(this->m_path, std::ios::binary | std::ios::ate);
	if(!file.is_open())
	{
		LOG("Failed to open ROM file " << m_path)
		return false;
	}
	file.seekg(_offset);
	if(file.tellg() != _offset)
		return false;
	file.read(reinterpret_cast<char *>(_out.data()), 256);
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

std::string ROMFile::getPresetName(const TPreset& _preset, uint32_t _first, uint32_t _last)
{
	std::string name;

	name.reserve(11);

	for (uint32_t i = _first; i <= _last; i++)
	{
		const char c = _preset[i];
		if(c < 32 || c > 127)
			break;
		name.push_back(c);
	}

	return name;
}

}
