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

	using TAudioOutputs = std::array<std::vector<dsp56k::TWord>, 6>;
	using TAudioInputs = std::array<std::vector<dsp56k::TWord>, 2>;

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
