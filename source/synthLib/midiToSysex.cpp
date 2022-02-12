#include "midiToSysex.h"

#include <cstdio>

#include "dsp56kEmu/logging.h"

namespace synthLib
{
	bool MidiToSysex::readFile(std::vector<uint8_t>& _sysexMessages, const char* _filename)
	{
		FILE* hFile = fopen(_filename, "rb");

		if (hFile == nullptr)
		{
			LOG("Failed to open file " << _filename);
			return false;
		}

		if (!checkChunk(hFile,"MThd"))
		{
			fclose(hFile);	// file isn't midi file
			hFile = nullptr;
			return false;
		}
		// ok, file is a midi file, we can start reading

		// skip the rest of the id chunk
		ignoreChunk(hFile);

		while (!feof(hFile))
		{
			if (checkChunk(hFile, "MTrk"))
			{
				// ignore the length of the MTrk chunk, we don't need it
				char temp[4];
				fread(temp, 4, 1, hFile);

				bool bReadNextEvent = true;
				while (bReadNextEvent)
				{
					int numBytesRead;
					const auto timestamp = readVarLen(hFile, &numBytesRead);

					if (timestamp == -1)
					{
						LOG("Failed to read variable length variable");
						fclose(hFile);
						hFile = nullptr;
						return false;
					}

					const uint8_t event = getc(hFile);
					switch (event)
					{
					case 0xf0: // that's what we're searching for, sysex data
						{
							const auto sysExLen = readVarLen(hFile, &numBytesRead);

							std::vector<uint8_t> sysex;
							sysex.resize(sysExLen + 1);

							sysex[0] = 0xf0;

							fread(&sysex[1], sysExLen, 1, hFile);

							_sysexMessages.insert(_sysexMessages.end(), sysex.begin(), sysex.end());
						}
						break;

					case 0xff:	// meta event
						{
							const auto metaEvent = getc(hFile);
							const auto eventLen = getc(hFile);

							switch (metaEvent)
							{
							case 0x2f:
								// track end
								bReadNextEvent = false;
								break;
							default:
								std::vector<char> buffer;
								buffer.resize(eventLen);
								fread(&buffer[0], eventLen, 1, hFile);
							}
						}
						break;
					default:
						// Other events like notes, .....
						break;
					}
				}
			}
			else if (!ignoreChunk(hFile))
				break;
		}
		fclose(hFile);
		return true;
	}

	bool MidiToSysex::checkChunk(FILE* hFile, const char* _pCompareChunk)
	{
		char readChunk[4];

		if (!fread(readChunk, 4, 1, hFile))
			return false;

		if (readChunk[0] == _pCompareChunk[0] && readChunk[1] == _pCompareChunk[1] &&
			readChunk[2] == _pCompareChunk[2] && readChunk[3] == _pCompareChunk[3])
			return true;

		return false;
	}
	uint32_t MidiToSysex::getChunkLength(FILE* hFile)
	{
		return ((uint32_t)getc(hFile) << 24 |
			(uint32_t)getc(hFile) << 16 |
			(uint32_t)getc(hFile) << 8 |
			(uint32_t)getc(hFile));
	}
	bool MidiToSysex::ignoreChunk(FILE* hFile)
	{
		uint32_t len = getChunkLength(hFile);

		if ((long)len == -1)
			return false;

		fseek(hFile, len, SEEK_CUR);

		if (feof(hFile))
			return false;

		return true;
	}

	int32_t MidiToSysex::readVarLen(FILE* hFile, int* _pNumBytesRead)
	{
		uint32_t value;
		uint8_t c;

		*_pNumBytesRead = 1;

		if ((value = getc(hFile)) & 0x80)
		{
			value &= 0x7F;
			do
			{
				value = (value << 7) + ((c = getc(hFile)) & 0x7F);
				*_pNumBytesRead += 1;
				if (feof(hFile))
				{
					*_pNumBytesRead = 0;
					return -1;
				}
			} while (c & 0x80);
		}
		return(value);
	}
}
