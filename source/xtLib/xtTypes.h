#pragma once

#include <cstdint>
#include <array>
#include <vector>

#include "dsp56kEmu/types.h"

#include "xtId.h"

namespace xt
{
	static constexpr uint32_t g_ramSize  = 0x00020000;
	static constexpr uint32_t g_romSize  = 0x00040000;
	static constexpr uint32_t g_romAddr  = 0x00100000;
	static constexpr uint32_t g_addrMask = 0x001fffff;

	template<size_t Count> using TAudioBuffer = std::array<std::vector<dsp56k::TWord>, Count>;
	using TAudioOutputs = TAudioBuffer<4>;
	using TAudioInputs = TAudioBuffer<2>;

	using WaveData = std::array<int8_t, 128>;
	using TableData = std::array<WaveId, 64>;
}
