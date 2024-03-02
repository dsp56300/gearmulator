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
		static void splitMultipleSysex(std::vector<std::vector<uint8_t>>& _dst, const std::vector<uint8_t>& _src, bool _isMidiFileData = false);
		static bool extractSysexFromFile(std::vector<std::vector<uint8_t>>& _messages, const std::string& _filename);
		static bool extractSysexFromData(std::vector<std::vector<uint8_t>>& _messages, const std::vector<uint8_t>& _data);
	private:
		static bool checkChunk(FILE* hFile, const char* _pCompareChunk);
		static uint32_t getChunkLength(FILE* hFile);
		static int32_t readVarLen(FILE* hFile, int* _pNumBytesRead);
		static void readVarLen(uint32_t& _numBytesRead, uint32_t& _result, const uint8_t* _data, size_t _numBytes);
		static bool ignoreChunk(FILE* hFile);
	};
}
