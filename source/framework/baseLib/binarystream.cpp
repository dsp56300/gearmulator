#include "binarystream.h"

namespace baseLib
{
	BinaryStream BinaryStream::readChunk()
	{
		Chunk chunk;
		chunk.read(*this);
		return std::move(chunk.data);
	}

	std::pair<BinaryStream, uint32_t> BinaryStream::tryReadChunkInternal(const char* _4Cc, const uint32_t _minVersion, const uint32_t _maxVersion)
	{
		Chunk chunk;
		chunk.read(*this);
		if(chunk.version < _minVersion || chunk.version > _maxVersion)
			return {};
		if(0 != strcmp(chunk.fourCC, _4Cc))
			return {};
		return {std::move(chunk.data), chunk.version};
	}

	template BinaryStream BinaryStream::tryReadChunk(char const(& _4Cc)[5], uint32_t _version);
	template std::pair<BinaryStream, uint32_t> BinaryStream::tryReadChunk(char const(& _4Cc)[5], uint32_t _minVersion, uint32_t _maxVersion);
}
