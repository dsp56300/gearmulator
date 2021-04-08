#include <cassert>
#include <fstream>

#include "accessVirus.h"

#include "../dsp56300/source/dsp56kEmu/logging.h"

AccessVirus::AccessVirus(const char* _path) : m_path(_path)
{
	LOG("Init access virus");
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

	return chunks;

}

AccessVirus::DSPProgram AccessVirus::get_dsp_program() const
{
	auto chunks = get_dsp_chunks();
	
	DSPProgram dspProgram;
	dspProgram.bootRom.size = chunks[0].items[0];
	dspProgram.bootRom.offset = chunks[0].items[1];
	dspProgram.bootRom.data = std::vector<uint32_t>(dspProgram.bootRom.size);

	// The first chunk contains the bootrom
	auto i = 2;
	for (; i < dspProgram.bootRom.size + 2; i++)
	{
		dspProgram.bootRom.data[i-2] = chunks[0].items[i];
	}

	// The rest of the chunks is made up of the command stream
	for (auto j = 0; j < chunks.size(); j++)
	{
		for (; i < chunks[j].items.size(); i++)
			dspProgram.commandStream.emplace_back(chunks[j].items[i]);
		i = 0;
	}

	return dspProgram;
}
