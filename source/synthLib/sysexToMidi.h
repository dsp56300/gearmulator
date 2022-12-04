#pragma once

#include <ostream>
#include <vector>

namespace synthLib
{
	class SysexToMidi
	{
	public:
		static bool write(const char* _filename, const std::vector<std::vector<uint8_t>>& _messages);
		static void write(std::ostream& _dst, const std::vector<std::vector<uint8_t>>& _messages);
	private:
		static void writeBuf(std::ostream& _dst, const std::vector<uint8_t>& _data);
		static void writeUInt8(std::ostream& _dst, uint8_t _data);
		static void writeUInt32(std::ostream& _dst, const uint32_t& _data);
		static void writeUInt16(std::ostream& _dst, const uint16_t& _data);
		static void write(std::ostream& _dst, const std::string& _data);
		static void writeVarLen(std::ostream& _dst, uint32_t _len);
	};
}
