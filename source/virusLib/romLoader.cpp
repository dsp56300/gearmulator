#include "romLoader.h"

#include "../dsp56300/source/dsp56kEmu/memory.h"

namespace virusLib
{
	bool RomLoader::loadFromFile(dsp56k::Memory& _memory, const char* _filename)
	{
		// @steven your magic goes here.
		_memory.set(dsp56k::MemArea_P, 0x000000, 0xabcdef);

		return false;
	}
}
