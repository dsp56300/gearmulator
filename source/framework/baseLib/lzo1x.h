#pragma once

#include <cstdint>
#include <vector>

namespace baseLib
{
	// LZO1X decompression, the format that lzo1x_1 up to lzo1x_999 produce. The stream has to end with its end marker
	// exactly at the end of _src. Returns false for anything malformed, _dst gets exactly what the stream produces
	bool lzo1xDecompress(const std::vector<uint8_t>& _src, std::vector<uint8_t>& _dst);
}
