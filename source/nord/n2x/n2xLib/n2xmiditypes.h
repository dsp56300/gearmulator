#pragma once

#include <cstdint>

namespace n2x
{
	enum SysexByte : uint8_t
	{
		IdClavia = 0x33,
		IdN2X = 0x04,
		SingleDumpBankEditBuffer    = 0x00, SingleDumpBankA    = 0x01, SingleDumpBankB    = 0x02, SingleDumpBankC    = 0x03, SingleDumpBankD    = 0x04,
//		SingleRequestBankEditBuffer = 0x0a, SingleRequestBankA = 0x0b, SingleRequestBankB = 0x0c, SingleRequestBankC = 0x0d, SingleRequestBankD = 0x0e,
		DefaultDeviceId = 0xf
	};
}
