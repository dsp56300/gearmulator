#pragma once

#include <cstdint>

namespace n2x
{
	enum SysexByte : uint8_t
	{
		IdClavia = 0x33,
		IdN2X = 0x04,
		SingleDumpBankEditBuffer    = 0x00, SingleDumpBankA    = 0x01, SingleDumpBankB    = 0x02, SingleDumpBankC    = 0x03, SingleDumpBankD    = 0x04,
		SingleRequestBankEditBuffer = 0x0e, SingleRequestBankA = 0x0f, SingleRequestBankB = 0x10, SingleRequestBankC = 0x11, SingleRequestBankD = 0x12,
		MultiRequestBankEditBuffer = 0x28,
		DefaultDeviceId = 0xf
	};
}
