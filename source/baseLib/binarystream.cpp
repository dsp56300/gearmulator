#include "binarystream.h"

namespace baseLib
{
	BinaryStream BinaryStream::readChunk()
	{
		Chunk chunk;
		chunk.read(*this);
		return std::move(chunk.data);
	}

	BinaryStream BinaryStream::tryReadChunkInternal(const char* _4Cc, const uint32_t _version)
	{
		Chunk chunk;
		chunk.read(*this);
		if(chunk.version != _version)
			return {};
		if(0 != strcmp(chunk.fourCC, _4Cc))
			return {};
		return std::move(chunk.data);
	}

	template BinaryStream BinaryStream::tryReadChunk(char const(& _4Cc)[5], uint32_t _version);
}
