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
		const void* data = nullptr;
		size_t dataByteSize = 0;
		uint32_t bitsPerSample = 0;
		uint32_t channels = 0;
		uint32_t samplerate = 0;
		bool isFloat = false;
	};

	class WavLoader
	{
	public:
		static bool load(Data& _data, std::vector<CuePoint>* _cuePoints, const uint8_t* _buffer, size_t _bufferSize);
	};
};
