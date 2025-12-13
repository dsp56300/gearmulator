#include "sounddiverLibLoader.h"

#include "vstpreset.h"

#include "baseLib/binarystream.h"

#include "dsp56kBase/logging.h"

namespace synthLib
{
	// https://gist.github.com/arteme/76f59209db3fe263c82ee151e35a1723

	namespace
	{
		bool check4CC(const char* _4CC, const char* _expected)
		{
			return 0 == strcmp(_4CC, _expected);
		}
	}
	SounddiverLibLoader::SounddiverLibLoader(Data _input)
		: m_input(std::move(_input))
		, m_stream(m_input)
	{
		if (m_input.size() < 8)
			return;	// file too small

		try
		{
			char fourCC[5];
			m_stream.read4CC(fourCC);

			if (check4CC(fourCC, "FORM"))
				m_littleEndian = false;
			else if (check4CC(fourCC, "MROF"))
				m_littleEndian = true;
			else
				return; // not a valid Sounddiver file

			m_stream.setReadPos(0);

			std::vector<Chunk> chunks;

			while (!m_stream.endOfStream())
			{
				Chunk chunk;
				read4CC(chunk.fourCC);
				const auto length = readUInt32();

				if (length > m_input.size())
					throw std::runtime_error("chunk length of size " + std::to_string(length) + " too large for data size " + std::to_string(m_input.size()));

				chunk.data.resize(length);
				m_stream.read(chunk.data.data(), length);

				chunks.emplace_back(std::move(chunk));
			}

			if (chunks.size() != 1)
				return;	// unexpected, we expect a single FORM/MROF chunk

			auto rootChunk = std::move(chunks.front());
			chunks.clear();

			m_stream = baseLib::BinaryStream(rootChunk.data);

			read4CC(fourCC);
			if (!check4CC(fourCC, "SSLB"))
				return; // not a valid Sounddiver file

			while (!m_stream.endOfStream())
			{
				Chunk chunk;
				read4CC(chunk.fourCC);
				const auto length = readUInt32();
				if (length > m_input.size())
					throw std::runtime_error("chunk length of size " + std::to_string(length) + " too large for data size " + std::to_string(m_input.size()));
				chunk.data.resize(length);
				m_stream.read(chunk.data.data(), length);
				chunks.emplace_back(std::move(chunk));
			}

			for (const auto& chunk : chunks)
			{
				if (!check4CC(chunk.fourCC, "LENT"))
					continue;

				m_stream = baseLib::BinaryStream(chunk.data);

				if (chunk.data.size() < 12)
					continue; // chunk broken, 12 bytes header is minimum

				ListEntry e;

				e.entryType = readUInt8();
				e.modelTypeA = readUInt8();
				e.modelTypeB = readUInt8();
				e.unknown3 = readUInt8();
				auto dateTime = readUInt32();
				e.unknown8 = readUInt8();
				e.unknown9 = readUInt8();
				e.deviceId = readUInt8();
				e.unknown11 = readUInt8();

				// dateTime is a 32 bit value: (MSB to LSB)
				// 7 bits for year-1980 (0-127)
				// 4 bits for month (1-12)
				// 5 bits for day (1-31)
				// 5 bits for hour (0-23)
				// 6 bits for minutes (0-59)
				// 5 bits for seconds/2 (0-29)

				e.year = ((dateTime >> 25) & 0x7f) + 1980;
				e.month = (dateTime >> 21) & 0x0f;
				e.day = (dateTime >> 16) & 0x1f;
				e.hour = (dateTime >> 11) & 0x1f;
				e.minute = (dateTime >> 5) & 0x3f;
				e.second = (dateTime & 0x1f) * 2;

				while (!m_stream.endOfStream())
				{
					const auto type = readUInt8();

					switch (type)
					{
					case 0: // closing chunk
						while (!m_stream.endOfStream())
							readUInt8();
						break;
					case 1:	// name
					case 2:	// location
					case 6:	// comment
						{
							std::string s;
							const auto len = readVarLen();
							if (len > chunk.data.size())
								throw std::runtime_error("string length of size " + std::to_string(len) + " too large for chunk data size " + std::to_string(chunk.data.size()));
							s.reserve(len);
							for (size_t i = 0; i < len; ++i)
								s += static_cast<char>(readUInt8());
							switch (type)
							{
							case 1:	e.name = std::move(s); break;
							case 2:	e.location = std::move(s); break;
							case 6:	e.comment = std::move(s); break;
							default:;
							}
						}
						break;
					case 3:	// module specific sound data
						{
							const auto len = readVarLen();
							if (len > chunk.data.size())
								throw std::runtime_error("data length of size " + std::to_string(len) + " too large for chunk data size " + std::to_string(chunk.data.size()));
							e.data.resize(len);
							for (size_t i = 0; i < len; ++i)
								e.data[i] = readUInt8();
						}
						break;
					default:
						LOG("SounddiverLibLoader, unknown LENT chunk data block type " << static_cast<int>(type) << ", skipping remaining data of LENT chunk");
						while (!m_stream.endOfStream())
							readUInt8();
						break;
					}
				}

				m_listEntries.emplace_back(std::move(e));
			}			
		}
		catch (std::runtime_error& e)
		{
			LOG("SounddiverLibLoader, error: " << e.what());
		}
	}

	bool SounddiverLibLoader::isValidData(const Data& _data)
	{
		if (_data.size() < 8)
			return false;	// file too small
		char fourCC[5];
		memcpy(fourCC, _data.data(), 4);
		fourCC[4] = 0;
		return check4CC(fourCC, "FORM") || check4CC(fourCC, "MROF");
	}

	void SounddiverLibLoader::read4CC(char* _dest)
	{
		char fourCC[5] = {};

		m_stream.read4CC(fourCC);

		if (m_littleEndian)
		{
			std::swap(fourCC[0], fourCC[3]),
			std::swap(fourCC[1], fourCC[2]);
		}

		_dest[0] = fourCC[0];
		_dest[1] = fourCC[1];
		_dest[2] = fourCC[2];
		_dest[3] = fourCC[3];
		_dest[4] = 0;
	}

	size_t SounddiverLibLoader::readUInt32()
	{
		const auto i = m_stream.read<uint32_t>();

		if (m_littleEndian == (VstPreset::hostEndian() == VstPreset::Endian::Little))
			return i;

		return
			((i & 0xFF000000) >> 24) |
			((i & 0x00FF0000) >> 8) |
			((i & 0x0000FF00) << 8) |
			((i & 0x000000FF) << 24);
	}

	uint8_t SounddiverLibLoader::readUInt8()
	{
		return m_stream.read<uint8_t>();
	}

	size_t SounddiverLibLoader::readVarLen()
	{
		size_t value = 0;
		uint8_t c;
		do
		{
			c = readUInt8();
			value = (value << 7) + (c & 0x7f);
		} while (c & 0x80);
		return value;
	}
}
