#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wLib
{
	class ROM
	{
	public:
		explicit ROM(const std::string& _filename, const uint32_t _expectedSize, const uint8_t* _data = nullptr) : m_data(_data)
		{
			if (!_filename.empty())
				loadFromFile(_filename, _expectedSize);
		}
		virtual ~ROM() = default;

		const uint8_t* getData() const { return m_data; }
		bool isValid() const { return !m_buffer.empty(); }
		virtual uint32_t getSize() const = 0;

		static bool loadFromMidi(std::vector<uint8_t>& _buffer, const std::string& _filename);
		static bool loadFromSysExFile(std::vector<uint8_t>& _buffer, const std::string& _filename);
		static bool loadFromSysExBuffer(std::vector<uint8_t> &_buffer, const std::vector<uint8_t> &_sysex);

	private:
		bool loadFromFile(const std::string& _filename, uint32_t _expectedSize);

		const uint8_t* m_data;
		std::vector<uint8_t> m_buffer;
	};	
}
