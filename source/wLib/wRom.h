#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wLib
{
	class ROM
	{
	public:
		explicit ROM(const std::string& _filename, const uint32_t _expectedSize, std::vector<uint8_t> _data = {}) : m_buffer(std::move(_data))
		{
			if (m_buffer.size() != _expectedSize)
				loadFromFile(_filename, _expectedSize);
			else
				m_data = m_buffer.data();
		}
		virtual ~ROM() = default;

		const uint8_t* getData() const { return m_data; }
		bool isValid() const { return !m_buffer.empty(); }
		virtual uint32_t getSize() const = 0;

		static bool loadFromMidi(std::vector<uint8_t>& _buffer, const std::string& _filename);
		static bool loadFromMidiData(std::vector<uint8_t>& _buffer, const std::vector<uint8_t>& _midiData);
		static bool loadFromSysExFile(std::vector<uint8_t>& _buffer, const std::string& _filename);
		static bool loadFromSysExBuffer(std::vector<uint8_t> &_buffer, const std::vector<uint8_t> &_sysex, bool _isMidiFileData = false);

	private:
		bool loadFromFile(const std::string& _filename, uint32_t _expectedSize);

		const uint8_t* m_data;
		std::vector<uint8_t> m_buffer;
	};	
}
