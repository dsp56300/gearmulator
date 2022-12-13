#pragma once

namespace mqLib
{
	enum class MidiDataType
	{
		SingleRequest = 0x00, SingleDump = 0x10, SingleParameterChange = 0x20, SingleParameterRequest = 0x30,
		MultiRequest  = 0x01, MultiDump  = 0x11, MultiParameterChange  = 0x21, MultiParameterRequest  = 0x31,
		DrumRequest	  = 0x02, DrumDump   = 0x12, DrumParameterChange   = 0x22, DrumParameterRequest   = 0x32,
		GlobalRequest = 0x04, GlobalDump = 0x14, GlobalParameterChange = 0x24, GlobalParameterRequest = 0x34,
	};

	enum class MidiBufferNum
	{
		SingleEditBuffer = 0x20,
		MultiEditBuffer = 0x30,
		AllSingleBanks = 0x00,
		SingleBankA = 0x40,
		SingleBankB = 0x41,
		SingleBankC = 0x42,
		SingleBankX = 0x48
	};
}
