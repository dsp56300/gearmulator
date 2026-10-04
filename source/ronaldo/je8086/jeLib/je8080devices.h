#pragma once

#include <cstdint>

#include "je8086devices.h"

namespace jeLib::je8080
{
	enum SwitchType : uint8_t
	{
		kSwitch_Bank1 = 32,
		kSwitch_Number1 = 40,
		kSwitch_Shift = 52,
		kSwitch_Exit = 53
	};

	constexpr uint32_t SwitchRows = 8;

	enum FaderType : uint8_t
	{
		FaderAsic0EspDac = 0,
		FaderBatterySense = 8,
		FaderPortamentoTime = 30,
		FaderPortamentoTimeAlias = 31,
		FaderOscillatorBalance = 40,
		FaderCrossModulationDepth = 44,
		FaderLfo1Depth = 46,
		FaderTempo = 62,
		FaderTempoAlias = 63
	};

	inline devices::Faders::ChannelMap faderChannels()
	{
		devices::Faders::ChannelMap map{};
		for (uint8_t c = 0; c < map.size(); ++c)
		{
			uint8_t which = c;
			if ((which & 0x38) == 0) which = FaderAsic0EspDac;
			if ((which & 0x38) == 8) which = FaderBatterySense;
			if ((which & 0x3c) == FaderOscillatorBalance) which = FaderOscillatorBalance;
			if ((which & 0x3e) == FaderCrossModulationDepth) which = FaderCrossModulationDepth;
			if ((which & 0x3e) == FaderLfo1Depth) which = FaderLfo1Depth;
			if (which == FaderTempoAlias) which = FaderTempo;
			if (which == FaderPortamentoTimeAlias) which = FaderPortamentoTime;
			map[c] = which;
		}
		return map;
	}
}
