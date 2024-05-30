#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pluginLib
{
	class Clipboard
	{
	public:
		static std::string midiDataToString(const std::vector<uint8_t>& _data, uint32_t _bytesPerLine = 32);
		static std::vector<std::vector<uint8_t>> getSysexFromString(const std::string& _text);
	};
}
