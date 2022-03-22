#pragma once

#include <vector>
#include <iostream>
#include <cstdint>

namespace synthLib
{
	class MidiToSysex
	{
	public:
		static bool readFile(std::vector<uint8_t>& _sysexMessages, const char* _filename);
		static void splitMultipleSysex(std::vector<std::vector<uint8_t>>& _dst, const std::vector<uint8_t>& _src);
	private:
		static bool checkChunk(FILE* hFile, const char* _pCompareChunk);
		static uint32_t getChunkLength(FILE* hFile);
		static int32_t readVarLen(FILE* hFile, int* _pNumBytesRead);
		static bool ignoreChunk(FILE* hFile);
	};
}
