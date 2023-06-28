#include "rom.h"

#include "mqmiditypes.h"
#include "../synthLib/os.h"
#include "../synthLib/midiToSysex.h"

namespace mqLib
{
	bool ROM::loadFromFile(const std::string& _filename)
	{
		FILE* hFile = fopen(_filename.c_str(), "rb");
		if(!hFile)
			return false;

		fseek(hFile, 0, SEEK_END);
		const auto size = ftell(hFile);
		fseek(hFile, 0, SEEK_SET);

		m_buffer.resize(size);
		const auto numRead = fread(m_buffer.data(), 1, size, hFile);
		fclose(hFile);

		if(numRead != static_cast<size_t>(size))
		{
			m_buffer.clear();
			return false;
		}

		if(numRead != getSize())
		{
			loadFromMidi(m_buffer, _filename);

			if (!m_buffer.empty() && m_buffer.size() < getSize())
				m_buffer.resize(getSize(), 0xff);
		}

		if(!m_buffer.empty())
		{
			m_data = m_buffer.data();
			return true;
		}
		return false;
	}

	bool ROM::loadFromMidi(std::vector<unsigned char>& _buffer, const std::string& _filename)
	{
		_buffer.clear();

		std::vector<uint8_t> data;
		if(!synthLib::MidiToSysex::readFile(data, _filename.c_str()) || data.empty())
			return false;

		return loadFromSysExBuffer(_buffer, data);
	}

	bool ROM::loadFromSysExFile(std::vector<uint8_t>& _buffer, const std::string& _filename)
	{
		_buffer.clear();

		std::vector<uint8_t> buf;
		if (!synthLib::readFile(buf, _filename))
			return false;
		return loadFromSysExBuffer(_buffer, buf);
	}

	bool ROM::loadFromSysExBuffer(std::vector<unsigned char>& _buffer, const std::vector<uint8_t>& _sysex)
	{
		_buffer.reserve(getSize());

		std::vector<std::vector<uint8_t>> messages;
		synthLib::MidiToSysex::splitMultipleSysex(messages, _sysex);

		uint16_t expectedCounter = 1;

		for (const auto& message : messages)
		{
			if(message.size() < 0xfc)
				continue;

			if(message[1] != IdWaldorf)
				continue;

			if(message[3] != 0x7f)
				continue;

			if(message[4] != 0x71 && message[4] != 0x72 && message[4] != 0x73)		// MW2, Q, mQ
				continue;
			
			const auto counter = (message[6] << 7) | message[7];
			if(expectedCounter != counter && counter != 1)
				return false;
			expectedCounter = static_cast<uint16_t>(counter);
			++expectedCounter;

			size_t i = 10;
			while(i + 5 < message.size())
			{
				const auto lsbs = message[i];
				_buffer.push_back((message[i+1] << 1) | ((lsbs >> 0) & 1));
				_buffer.push_back((message[i+2] << 1) | ((lsbs >> 1) & 1));
				_buffer.push_back((message[i+3] << 1) | ((lsbs >> 2) & 1));
				_buffer.push_back((message[i+4] << 1) | ((lsbs >> 3) & 1));
				i += 5;
			}
		}

		return true;
	}
}
