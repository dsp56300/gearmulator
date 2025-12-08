#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <optional>

#include "baseLib/binarystream.h"

namespace synthLib
{
	class VstPreset
	{
	public:
		using ChunkData = std::vector<uint8_t>;
		using FourCC = std::array<char, 5>;	// + null terminator

		struct Chunk
		{
			FourCC id = {0,0,0,0,0};
			ChunkData data;
		};

		using ChunkList = std::vector<Chunk>;

		static std::optional<ChunkList> read(const std::vector<uint8_t>& _data);

		static std::optional<ChunkList> readFxbFxp(baseLib::BinaryStream& _binaryStream);
		static std::optional<ChunkList> readVst3(baseLib::BinaryStream& _binaryStream);
	};
}
