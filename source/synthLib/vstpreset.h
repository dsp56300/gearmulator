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

		enum class Endian : uint8_t
		{
			Big,
			Little
		};

		static std::optional<ChunkList> read(const std::vector<uint8_t>& _data);

		static std::optional<ChunkList> readFxbFxp(baseLib::BinaryStream& _binaryStream);
		static std::optional<ChunkList> readVst3(baseLib::BinaryStream& _binaryStream);

		static constexpr Endian hostEndian()
		{
			constexpr uint32_t test32 = 0x01020304;
			constexpr uint8_t test8 = static_cast<const uint8_t&>(test32);

			static_assert(test8 == 0x01 || test8 == 0x04, "unable to determine endianess");

			return test8 == 0x01 ? Endian::Big : Endian::Little;
		}
	};
}
