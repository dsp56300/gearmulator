#pragma once

#include <cstdint>

namespace n2x
{
	enum SysexByte : uint8_t
	{
		IdClavia = 0x33, IdN2X = 0x04, DefaultDeviceId = 0xf,

		SingleDumpBankEditBuffer    = 0x00, SingleDumpBankA    = 0x01, SingleDumpBankB    = 0x02, SingleDumpBankC    = 0x03, SingleDumpBankD    = 0x04,
		SingleRequestBankEditBuffer = 0x0e, SingleRequestBankA = 0x0f, SingleRequestBankB = 0x10, SingleRequestBankC = 0x11, SingleRequestBankD = 0x12,

		MultiDumpBankEditBuffer    = 30,
		MultiRequestBankEditBuffer = 40,
	};

	enum SysexIndex
	{
		IdxClavia = 1,
		IdxDevice,
		IdxN2x,
		IdxMsgType,
		IdxMsgSpec,
	};

	static constexpr uint32_t g_sysexHeaderSize = 6;												// F0, IdClavia, IdDevice, IdN2x, MsgType, MsgSpec
	static constexpr uint32_t g_sysexFooterSize = 1;												// F7
	static constexpr uint32_t g_sysexContainerSize = g_sysexHeaderSize + g_sysexFooterSize;

	static constexpr uint32_t g_singleDataSize = 66 * 2;											// 66 parameters in two nibbles
	static constexpr uint32_t g_multiDataSize = 4 * g_singleDataSize + 90 * 2;						// 4 singles and 90 params in two nibbles

	static constexpr uint32_t g_singleDumpSize = g_singleDataSize + g_sysexContainerSize;
	static constexpr uint32_t g_multiDumpSize  = g_multiDataSize  + g_sysexContainerSize;
	static constexpr uint32_t g_patchRequestSize = g_sysexContainerSize;

	static constexpr uint32_t g_singleBankCount = 10;
	static constexpr uint32_t g_multiBankCount = 4;
	static constexpr uint32_t g_programsPerBank = 99;
}
