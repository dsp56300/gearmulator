#include "wavReader.h"

#include <cstring>
#include <map>
#include <cassert>

#include "wavTypes.h"

#include "dsp56kEmu/logging.h"

namespace synthLib
{
bool WavLoader::load(Data& _data, std::vector<CuePoint>* _cuePoints, const uint8_t* _buffer, size_t _bufferSize)
{
//	const unsigned int totalFileLength = _bufferSize;

	size_t bufferPos = 0;

	SWaveFormatHeader& header = *(SWaveFormatHeader*)&_buffer[0];
	bufferPos += sizeof(header);

	if( memcmp( header.str_wave, "WAVE", 4 ) != 0 )
		return false;

	if( memcmp( header.str_riff, "RIFF", 4 ) != 0 )
		return false;

	std::map<uint32_t, SWaveFormatChunkCuePoint> cuePoints;
	std::map<uint32_t, std::string> labels;

	size_t dataChunkOffset = 0;
	size_t dataChunkSize = 0;
	size_t formatChunkOffset = 0;

	while( bufferPos < (_bufferSize - sizeof(SWaveFormatChunkInfo)) )
	{
		SWaveFormatChunkInfo& chunkInfo = *(SWaveFormatChunkInfo*)&_buffer[bufferPos];
		bufferPos += sizeof(chunkInfo);

		if (memcmp(chunkInfo.chunkName, "fmt ", 4) == 0)
		{
			formatChunkOffset = bufferPos;
		}
		else if (memcmp(chunkInfo.chunkName, "data", 4) == 0)
		{
			dataChunkOffset = bufferPos;
			dataChunkSize = chunkInfo.chunkSize;
		}
		else if (memcmp(chunkInfo.chunkName, "cue ", 4) == 0)
		{
			SWaveFormatChunkCue& chunkCue = *(SWaveFormatChunkCue*)&_buffer[bufferPos];

			size_t offset = bufferPos + sizeof(chunkCue);

			for (size_t i = 0; i < chunkCue.cuePointCount; ++i)
			{
				SWaveFormatChunkCuePoint& point = *(SWaveFormatChunkCuePoint*)&_buffer[offset];

				if (cuePoints.find(point.cueId) == cuePoints.end())
					cuePoints.insert(std::make_pair(point.cueId, point));
				else
					LOG("Warning: duplicated cue point " << point.cueId << " found");

				offset += sizeof(point);
			}
		}
		else if (_cuePoints && (memcmp(chunkInfo.chunkName, "list", 4) == 0 || memcmp(chunkInfo.chunkName, "LIST", 4) == 0))
		{
			SWaveFormatChunkList& adtl = *(SWaveFormatChunkList*)&_buffer[bufferPos];

			size_t offset = bufferPos + sizeof(adtl);

			while (offset < (bufferPos + chunkInfo.chunkSize))
			{
				SWaveFormatChunkInfo& subChunkInfo = *(SWaveFormatChunkInfo*)&_buffer[offset];

				offset += sizeof(subChunkInfo);

				if (memcmp(subChunkInfo.chunkName, "labl", 4) == 0 || memcmp(subChunkInfo.chunkName, "note", 4) == 0)
				{
					SWaveFormatChunkLabel& chunkLabel = *(SWaveFormatChunkLabel*)&_buffer[offset];
					std::vector<char> name;
					name.resize(subChunkInfo.chunkSize- sizeof(chunkLabel));
					::memcpy(&name[0], &_buffer[offset + sizeof(chunkLabel)], name.size());

					while (!name.empty() && name.back() == 0)
						name.pop_back();

					std::string nameString;
					nameString.insert(nameString.begin(), name.begin(), name.end());

					if (!nameString.empty())
					{
						if (labels.find(chunkLabel.cuePointId) == labels.end())
						{
							labels.insert(std::make_pair(chunkLabel.cuePointId, nameString));
						}
						else
						{
							LOG("Warning: duplicated cue label " << nameString << " found for cue id " << chunkLabel.cuePointId);;
						}
					}
				}
				offset += (subChunkInfo.chunkSize + 1) & ~1;
			}
		}

		bufferPos += (chunkInfo.chunkSize + 1) & ~1;
	}

	if (dataChunkOffset == 0)
	{
		LOG("Failed to find wave data in file");
		return false;
	}

	if (formatChunkOffset == 0)
	{
		LOG("Failed to find format information in file");
		return false;
	}

	SWaveFormatChunkFormat& fmt = *(SWaveFormatChunkFormat*)&_buffer[formatChunkOffset];
	bufferPos += sizeof(fmt);

	if( fmt.wave_type != eFormat_PCM && fmt.wave_type != eFormat_IEEE_FLOAT )
		return false;	// Not PCM or float data

	_data.samplerate = fmt.sample_rate;

	bufferPos = dataChunkOffset;

	const uint32_t numBytes			= static_cast<uint32_t>(dataChunkSize);
	const uint32_t numSamples		= (numBytes << 3) / fmt.bits_per_sample;

	int numChannels = fmt.num_channels;

	_data.data = &_buffer[bufferPos];
	_data.dataByteSize = numBytes;
	_data.bitsPerSample = fmt.bits_per_sample;
	_data.channels = fmt.num_channels;
	_data.isFloat = fmt.wave_type == eFormat_IEEE_FLOAT;

	if (_cuePoints && !cuePoints.empty())
	{
		// So this is a bit wierd, according to the doc at https://sites.google.com/site/musicgapi/technical-documents/wav-file-format
		// The sampleOffset should be in bytes for uncompressed data and the position should be 0 if there is no plst chunk
		// Adobe Audition writes both sampleOffset and position and they are both in samples, even though or wav file 
		// is uncompressed (32bit float though) and there is no plst chunk so for now treat it that way for float data

		_cuePoints->clear();
		for (auto it = cuePoints.begin(); it != cuePoints.end(); ++it)
		{
			const SWaveFormatChunkCuePoint& src = it->second;
			CuePoint dst;

			if (src.playOrderPosition > 0 || src.playOrderPosition == src.sampleOffset)
				dst.sampleOffset = src.playOrderPosition;
			else
				dst.sampleOffset = src.sampleOffset * numSamples / (fmt.bits_per_sample >> 3);

			const auto itName = labels.find(it->first);

			if (itName != labels.end())
				dst.name = itName->second;

			_cuePoints->push_back(dst);
		}
	}

	return true;
}

}
