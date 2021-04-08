#pragma once

namespace dsp56k
{
	class Memory;
}

namespace virusLib
{
	class RomLoader
	{
	public:
		bool loadFromFile(dsp56k::Memory& _memory, const char* _filename);
	};
}
