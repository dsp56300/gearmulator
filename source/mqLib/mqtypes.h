#pragma once

#include <array>
#include <vector>

#include "dsp56kEmu/types.h"

namespace mqLib
{
	enum PortBits
	{
		// Port E
		Encoders0CS = 0,
		Encoders1CS = 1,
		Buttons0CS = 2,
		Buttons1CS = 3,
		BtPower = 5,						// Power Button
		LedPower = 6,

		// Port F
		LcdRS = 1,
		LcdRW = 2,
		LcdLatch = 3,
		LedWriteLatch = 7,

		// Port QS
		DspNMI = 6,
	};

	template<size_t Count> using TAudioBuffer = std::array<std::vector<dsp56k::TWord>, Count>;

	using TAudioOutputs = TAudioBuffer<6>;
	using TAudioInputs = TAudioBuffer<2>;

	enum class BootMode
	{
		Default,
		FactoryTest,
		EraseFlash,
		WaitForSystemDump,
		DspClockResetAndServiceMode,
		ServiceMode,
		MemoryGame
	};
}
