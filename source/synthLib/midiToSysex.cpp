#include "midiToSysex.h"

#include <cstdio>
#include <cstring>	// memcmp

#include "dsp56kEmu/logging.h"

#include "os.h"

namespace synthLib
{
#ifdef _MSC_VER
#include <Windows.h>
	std::wstring ToUtf16(const std::string& str)
	{
		std::wstring ret;
		const int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
		if (len > 0)
		{
			ret.resize(len);
			MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &ret[0], len);
		}
		return ret;
	}
#endif

	bool MidiToSysex::readFile(std::vector<uint8_t>& _sysexMessages, const char* _filename)
	{
#ifdef _MSC_VER
		FILE* hFile = _wfopen(ToUtf16(_filename).c_str(), L"rb");
#else
		FILE* hFile = fopen(_filename, "rb");
#endif

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

					const auto ch = getc(hFile);

					if (ch == EOF)
						break;

					const auto ev = static_cast<uint8_t>(ch);

					switch (ev)
					{
					case 0xf0: // that's what we're searching for, sysex data
						{
							const auto sysExLen = readVarLen(hFile, &numBytesRead);

							std::vector<uint8_t> sysex;
							sysex.reserve(sysExLen + 1);

							sysex.push_back(0xf0);

							// we ignore the provided sysex length as I've seen files that do not have the length encoded properly
							while(true)
							{
								const auto c = getc(hFile);

								if(c == 0xf7 || c == 0xf8)	// Virus Powercore writes f8 instead of f7
								{
									sysex.push_back(0xf7);
									_sysexMessages.insert(_sysexMessages.end(), sysex.begin(), sysex.end());
									break;
								}

								sysex.push_back(static_cast<uint8_t>(c));

								if (feof(hFile))
									break;
							}
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

	void MidiToSysex::splitMultipleSysex(std::vector<std::vector<uint8_t>>& _dst, const std::vector<uint8_t>& _src, const bool _isMidiFileData/* = false*/)
	{
		if(!_isMidiFileData)
		{
			std::vector<size_t> indices;

			for (size_t i = 0; i < _src.size(); ++i)
			{
				if (indices.size() & 1)
				{
					if (_src[i] == 0xf7)
						indices.push_back(i);
				}
				else if (_src[i] == 0xf0)
				{
					indices.push_back(i);
				}
			}

			if (indices.size() & 1)
				indices.pop_back();

			for(size_t i=0; i<indices.size(); i += 2)
			{
				auto& e =_dst.emplace_back();
				e.assign(_src.begin() + indices[i], _src.begin() + indices[i + 1] + 1);
			}
			return;
		}

		for (size_t i = 0; i < _src.size(); ++i)
		{
			if (_src[i] != 0xf0)
				continue;

			uint32_t numBytesRead = 0;
			uint32_t length = 0;

			readVarLen(numBytesRead, length, &_src[i + 1], _src.size() - i - 1);

			// do some simple validation here, I've seen midi files where sysex is stored without varlength encoding
			if (length == 0 || (numBytesRead > 1 && length < 128))
				numBytesRead = 0;

			const auto jStart = i + numBytesRead + 1;

			for(size_t j = jStart; j < _src.size(); ++j)
			{
				if(_src[j] <= 0xf0)
					continue;

				std::vector<uint8_t> entry;
				entry.reserve(j - jStart + 2);
				entry.push_back(0xf0);
				entry.insert(entry.end(), _src.begin() + jStart, _src.begin() + j);
				entry.push_back(0xf7);
				_dst.emplace_back(std::move(entry));
				i = j;
				break;
			}
		}
	}

	bool MidiToSysex::extractSysexFromFile(std::vector<std::vector<uint8_t>>& _messages, const std::string& _filename)
	{
		std::vector<uint8_t> data;

		if(!synthLib::readFile(data, _filename))
			return false;

		return extractSysexFromData(_messages, data);
	}

	bool MidiToSysex::extractSysexFromData(std::vector<std::vector<uint8_t>>& _messages, const std::vector<uint8_t>& _data)
	{
		constexpr uint8_t midiHeader[] = "MThd";
		const auto isMidiFile = _data.size() >= 4 && memcmp(_data.data(), midiHeader, 4) == 0;
		splitMultipleSysex(_messages, _data, isMidiFile);
		return !_messages.empty();
	}

	bool MidiToSysex::checkChunk(FILE* hFile, const char* _pCompareChunk)
	{
		char readChunk[4];

		if (fread(readChunk, 1, 4, hFile) != 4)
			return false;

		if (readChunk[0] == _pCompareChunk[0] && readChunk[1] == _pCompareChunk[1] &&
			readChunk[2] == _pCompareChunk[2] && readChunk[3] == _pCompareChunk[3])
			return true;

		return false;
	}
	uint32_t MidiToSysex::getChunkLength(FILE* hFile)
	{
		const auto a = getc(hFile);
		const auto b = getc(hFile);
		const auto c = getc(hFile);
		const auto d = getc(hFile);
		
		if(a == EOF || b == EOF || c == EOF || d == EOF)
			return -1;

		return (uint32_t(a) << 24 | uint32_t(b) << 16 | uint32_t(c) << 8 | uint32_t(d));
	}
	bool MidiToSysex::ignoreChunk(FILE* hFile)
	{
		uint32_t len = getChunkLength(hFile);

		if ((int32_t)len == -1)
			return false;

		fseek(hFile, len, SEEK_CUR);

		if (feof(hFile))
			return false;

		return true;
	}

	int32_t MidiToSysex::readVarLen(FILE* hFile, int* _pNumBytesRead)
	{
		if (feof(hFile))
		{
			*_pNumBytesRead = 0;
			return -1;
		}

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

	void MidiToSysex::readVarLen(uint32_t& _numBytesRead, uint32_t& _result, const uint8_t* _data, const size_t _numBytes)
	{
		_numBytesRead = 0;
		_result = 0;

		for(size_t i=0; i<_numBytes; ++i)
		{
			const auto b = _data[i];

			const uint32_t v = b & 0x7f;

			_result += v;

			++_numBytesRead;

			if (b & 0x80)
				_result <<= 7;
			else
				break;
		}
	}
}
