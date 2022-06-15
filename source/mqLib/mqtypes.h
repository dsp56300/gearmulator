#pragma once

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
}
