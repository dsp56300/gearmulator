#pragma once

namespace wLib
{
	enum MidiHeaderByte : uint8_t
	{
		IdWaldorf = 0x3e,
		IdDeviceOmni = 0x7f
	};

	enum SysexIndex
	{
		IdxSysexBegin = 0,
		IdxIdWaldorf = 1,
		IdxIdMachine = 2,
		IdxDeviceId = 3,
		IdxCommand = 4,

		// dumps / dump requests
		IdxBuffer = 5,
		IdxLocation = 6,
	};
}
