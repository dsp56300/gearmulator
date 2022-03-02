#pragma once

#include "wavTypes.h"

#include <vector>
#include <string>

namespace synthLib
{
	class WavWriter
	{
	public:
		bool write(const std::string& _filename, int _bitsPerSample, bool isFloat, int _channelCount, int _samplerate, const void* _data, size_t _dataSize);

		template<typename T>
		bool write(const std::string& _filename, int _bitsPerSample, bool isFloat, int _channelCount, int _samplerate, const std::vector<T>& _data)
		{
			return write(_filename, _bitsPerSample, isFloat, _channelCount, _samplerate, &_data[0], sizeof(T) * _data.size());
		}
	private:
		size_t m_existingDataSize = 0;
	};
};
