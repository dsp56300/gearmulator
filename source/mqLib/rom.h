#pragma once

#include <string>
#include <vector>

namespace mqLib
{
	class ROM
	{
	public:
		static constexpr uint32_t g_romSize = 524288;

		explicit ROM(const std::string& _filename, const uint8_t* _data = nullptr) : m_data(_data)
		{
			if (!_filename.empty())
				loadFromFile(_filename);
		}

		const uint8_t* getData() const { return m_data; }
		static constexpr uint32_t getSize() { return g_romSize; }
		bool isValid() const { return !m_buffer.empty(); }

		static bool loadFromMidi(std::vector<uint8_t>& _buffer, const std::string& _filename);
		static bool loadFromSysExFile(std::vector<uint8_t>& _buffer, const std::string& _filename);
		static bool loadFromSysExBuffer(std::vector<uint8_t> &_buffer, const std::vector<uint8_t> &_sysex);
	private:
		bool loadFromFile(const std::string& _filename);

		const uint8_t* m_data;
		std::vector<uint8_t> m_buffer;
	};
}
