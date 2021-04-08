#pragma once
#include <vector>

class AccessVirus
{
public:
	typedef struct Chunk
	{
		uint8_t chunk_id;
		uint8_t size1;
		uint8_t size2;
		std::vector<uint32_t> items;
	} Chunk;

	typedef struct BootRom
	{
		uint32_t size;
		uint32_t offset;
		std::vector<uint32_t> data;
	} BootRom;

	typedef struct DSPProgram
	{
		BootRom *bootRom;
		std::vector<uint32_t> commandStream;
	} DSPProgram;

	AccessVirus(const char* path);

	std::vector<Chunk *> get_dsp_chunks();

	DSPProgram * get_dsp_program();

private:
	const char* path;

};

