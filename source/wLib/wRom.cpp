#include "wRom.h"

#include <cstdint>

#include "baseLib/filesystem.h"

#include "synthLib/midiToSysex.h"

namespace wLib
{
	constexpr uint8_t IdWaldorf = 0x3e;

	bool ROM::loadFromFile(const std::string& _filename, const uint32_t _expectedSize)
	{
		if(_filename.empty())
			return false;

		if(!baseLib::filesystem::readFile(m_buffer, _filename))
			return false;

		if(m_buffer.size() != _expectedSize)
		{
			m_buffer.clear();

			loadFromMidi(m_buffer, _filename);

			if (!m_buffer.empty() && m_buffer.size() < _expectedSize)
				m_buffer.resize(_expectedSize, 0xff);
		}

		if(m_buffer.size() != _expectedSize)
			return false;
		m_filename = _filename;
		return true;
	}

	bool ROM::loadFromMidi(std::vector<unsigned char>& _buffer, const std::string& _filename)
	{
		_buffer.clear();

		std::vector<uint8_t> data;
		if(!synthLib::MidiToSysex::readFile(data, _filename.c_str()) || data.empty())
			return false;

		return loadFromSysExBuffer(_buffer, data);
	}

	bool ROM::loadFromMidiData(std::vector<uint8_t>& _buffer, const std::vector<uint8_t>& _midiData)
	{
		return loadFromSysExBuffer(_buffer, _midiData, true);
	}

	bool ROM::loadFromSysExFile(std::vector<uint8_t>& _buffer, const std::string& _filename)
	{
		_buffer.clear();

		std::vector<uint8_t> buf;
		if (!baseLib::filesystem::readFile(buf, _filename))
			return false;
		return loadFromSysExBuffer(_buffer, buf);
	}

	bool ROM::loadFromSysExBuffer(std::vector<unsigned char>& _buffer, const std::vector<uint8_t>& _sysex, bool _isMidiFileData/* = false*/)
	{
		_buffer.reserve(_sysex.size());

		std::vector<std::vector<uint8_t>> messages;
		synthLib::MidiToSysex::splitMultipleSysex(messages, _sysex, _isMidiFileData);

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
