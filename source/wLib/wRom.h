#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wLib
{
	class ROM
	{
	public:
		ROM() = default;
		explicit ROM(const std::string& _filename, const uint32_t _expectedSize, std::vector<uint8_t> _data = {}) : m_buffer(std::move(_data))
		{
			if (m_buffer.size() != _expectedSize)
				loadFromFile(_filename, _expectedSize);
		}
		virtual ~ROM() = default;

		const uint8_t* getData() const { return m_buffer.data(); }
		bool isValid() const { return !m_buffer.empty(); }
		virtual uint32_t getSize() const = 0;

		void clear() { m_buffer.clear(); }

		const auto& getFilename() const { return m_filename; }

		static bool loadFromMidi(std::vector<uint8_t>& _buffer, const std::string& _filename);
		static bool loadFromMidiData(std::vector<uint8_t>& _buffer, const std::vector<uint8_t>& _midiData);
		static bool loadFromSysExFile(std::vector<uint8_t>& _buffer, const std::string& _filename);
		static bool loadFromSysExBuffer(std::vector<uint8_t> &_buffer, const std::vector<uint8_t> &_sysex, bool _isMidiFileData = false);

	private:
		bool loadFromFile(const std::string& _filename, uint32_t _expectedSize);

		std::vector<uint8_t> m_buffer;
		std::string m_filename;
	};	
}
