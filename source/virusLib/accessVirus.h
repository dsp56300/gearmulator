#pragma once

#include <vector>

class AccessVirus
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

	explicit AccessVirus(const char* _path);

	BootRom bootRom;
	std::vector<uint32_t> commandStream;

private:
	std::vector<Chunk> get_dsp_chunks() const;

	const char* const m_path;
};

