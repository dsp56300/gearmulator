#include <cassert>
#include <fstream>

#include "accessVirus.h"

#include "../dsp56300/source/dsp56kEmu/logging.h"

AccessVirus::AccessVirus(const char* _path) : m_path(_path)
{
	LOG("Init access virus");

	auto chunks = get_dsp_chunks();
	
	bootRom.size = chunks[0].items[0];
	bootRom.offset = chunks[0].items[1];
	bootRom.data = std::vector<uint32_t>(bootRom.size);

	// The first chunk contains the bootrom
	auto i = 2;
	for (; i < bootRom.size + 2; i++)
	{
		bootRom.data[i-2] = chunks[0].items[i];
	}

	// The rest of the chunks is made up of the command stream
	for (auto j = 0; j < chunks.size(); j++)
	{
		for (; i < chunks[j].items.size(); i++)
			commandStream.emplace_back(chunks[j].items[i]);
		i = 0;
	}

	printf("Program BootROM size = 0x%x\n", bootRom.size);
	printf("Program BootROM offset = 0x%x\n", bootRom.offset);
	printf("Program BootROM len = 0x%lx\n", bootRom.data.size());
	printf("Program Commands len = 0x%lx\n", commandStream.size());
}

std::vector<AccessVirus::Chunk> AccessVirus::get_dsp_chunks() const
{
	uint32_t offset = 0x18000;

	// Open file
	std::ifstream file(this->m_path, std::ios::binary | std::ios::ate);

	std::vector<Chunk> chunks(5);

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

		chunks[i] = chunk;

		offset += 0x8000;
	}

	file.close();

	return chunks;

}

void AccessVirus::loadPreset(const int bank, const int no)
{
	uint32_t offset = 0x50000 + (bank * 0x8000) + (no * 0x100);

	// Open file
	std::ifstream file(this->m_path, std::ios::binary | std::ios::ate);
	file.seekg(offset);

	preset.clear();
	preset.resize(0x56);

	uint8_t buf[3];
	for (int i = 0; i <= 0x56; i++) {
		if (i == 0x56) {
			file.read(reinterpret_cast<char*>(buf), 1);
			preset[i] = (buf[0] << 16 | 0x0701);
		} else {
			file.read(reinterpret_cast<char*>(buf), 3);
			preset[i] = ((buf[0] << 16) | (buf[1] << 8) | buf[2]);
		}
	}

	file.close();
}
