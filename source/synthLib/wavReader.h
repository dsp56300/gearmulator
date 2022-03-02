#pragma once

#include <vector>
#include <string>

namespace synthLib
{
	struct CuePoint
	{
		size_t sampleOffset;
		std::string name;

		bool operator < (const CuePoint& _other) const
		{
			return sampleOffset < _other.sampleOffset;
		}
	};

	struct Data
	{
		const void* data;
		size_t dataByteSize;
		uint32_t bitsPerSample;
		uint32_t channels;
		uint32_t samplerate;
		bool isFloat;
	};

	class WavLoader
	{
	public:
		static bool load(Data& _data, std::vector<CuePoint>* _cuePoints, const uint8_t* _buffer, size_t _bufferSize);
	};
};
