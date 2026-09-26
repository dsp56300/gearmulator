#pragma once

#include <cstddef>
#include <cstdint>

namespace baseLib
{
	// Adler-32 as zlib computes it
	uint32_t adler32(const uint8_t* _data, size_t _size);
}
