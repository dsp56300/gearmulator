#include "pch.h"
#include "accessvirus.h"
#include <iostream>
#include <fstream>
#include "../dsp56kEmu/assert.h"
#include "../dsp56kEmu/logging.h"


AccessVirus::AccessVirus(const char * path)
{
	LOG("Init access virus");
	this->path = path;
}


std::vector<AccessVirus::Chunk *> AccessVirus::get_dsp_chunks()
{
	uint32_t offset = 0x18000;

	// Open file
	std::ifstream file(this->path, std::ios::binary | std::ios::ate);

	std::vector<Chunk *> chunks(5);

	// Read all the chunks, hardcoded to 4 for convenience
	for (int i = 0; i <= 4; i++)
	{
		file.seekg(offset);

		// Read buffer
		Chunk* chunk = new Chunk;
		//file.read(reinterpret_cast<char *>(&chunk->chunk_id), 1);
		file.read((char *)(&chunk->chunk_id), 1);
		file.read((char *)(&chunk->size1), 1);
		file.read((char *)(&chunk->size2), 1);

		assert(chunk->chunk_id == 4 - i);

		// Format uses a special kind of size where the first byte should be decreased by 1
		uint16_t len = (chunk->size1 - 1) << 8 | chunk->size2;
		chunk->items = std::vector<uint32_t>(len);

		uint8_t buf[3];
		for (uint32_t j = 0; j < len; j++)
		{
			file.read(reinterpret_cast<char *>(buf), 3);
			chunk->items[j] = (buf[0] << 16) | (buf[1] << 8) | buf[2];
		}

		chunks[i] = chunk;

		offset += 0x8000;
	}

	return chunks;

}

AccessVirus::DSPProgram * AccessVirus::get_dsp_program()
{
	std::vector<AccessVirus::Chunk *> chunks = this->get_dsp_chunks();
	
	DSPProgram * dspProgram = new DSPProgram;
	dspProgram->bootRom = new BootRom;
	dspProgram->commandStream = std::vector<uint32_t>();
	dspProgram->bootRom->size = chunks[0]->items[0];
	dspProgram->bootRom->offset = chunks[0]->items[1];
	dspProgram->bootRom->data = std::vector<uint32_t>(dspProgram->bootRom->size);

	// The first chunk contains the bootrom
	unsigned int i;
	for (i = 2; i < dspProgram->bootRom->size + 2; i++)
	{
		dspProgram->bootRom->data[i - 2] = chunks[0]->items[i];
	}

	// The rest of the chunks is made up of the command stream
	int idx = 0;
	for (unsigned int j = 0; j < chunks.size(); j++)
	{
		for (; i < chunks[j]->items.size(); i++)
		{
			dspProgram->commandStream.push_back(chunks[j]->items[i]);
			idx += 1;
		}
		i = 0;
	}

	return dspProgram;
}
