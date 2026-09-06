#pragma once

#include <cstdint>
#include <ostream>

#include "midiTypes.h"

namespace synthLib
{
	class SysexToMidi
	{
	public:
		static bool write(const char* _filename, const SysexBufferList& _messages);
		static void write(std::ostream& _dst, const SysexBufferList& _messages);
	private:
		static void writeBuf(std::ostream& _dst, const SysexBuffer& _data);
		static void writeUInt8(std::ostream& _dst, uint8_t _data);
		static void writeUInt32(std::ostream& _dst, const uint32_t& _data);
		static void writeUInt16(std::ostream& _dst, const uint16_t& _data);
		static void write(std::ostream& _dst, const std::string& _data);
		static void writeVarLen(std::ostream& _dst, uint32_t _len);
	};
}
