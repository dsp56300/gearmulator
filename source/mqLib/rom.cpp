#include "rom.h"

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
		const auto numRead = fread(&m_buffer[0], 1, size, hFile);
		fclose(hFile);

		if(numRead != static_cast<size_t>(size))
		{
			m_buffer.clear();
			return false;
		}

		if(numRead != getSize())
		{
			loadFromMidi(m_buffer, _filename);
		}

		if(!m_buffer.empty())
		{
			m_data = &m_buffer[0];
			return true;
		}
		return false;
	}

	bool ROM::loadFromMidi(std::vector<unsigned char>& _buffer, const std::string& _filename) const
	{
		_buffer.clear();
		_buffer.reserve(getSize());

		std::vector<uint8_t> data;
		if(!synthLib::MidiToSysex::readFile(data, _filename.c_str()) || data.empty())
			return false;

		std::vector<std::vector<uint8_t>> messages;
		synthLib::MidiToSysex::splitMultipleSysex(messages, data);

		uint16_t expectedCounter = 1;

		for (const auto& message : messages)
		{
			if(message.size() < 0xfc)
				continue;

			const auto counter = (message[6] << 7) | message[7];
			if(expectedCounter != counter)
				return false;
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

		if(_buffer.size() < getSize())
			_buffer.resize(getSize(), 0xff);
		return true;
	}
}
