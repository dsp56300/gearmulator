#pragma once

#include <cstdint>

namespace mc68k
{
	static constexpr uint32_t g_peripheralMask	= 0xfffff;

	static constexpr uint32_t g_peripheralBase	= 0xff000;

	static constexpr uint32_t g_gptBase			= 0xff900;
	static constexpr uint32_t g_simBase			= 0xffa00;
	static constexpr uint32_t g_qsmBase			= 0xffc00;

	static constexpr uint32_t g_gptSize			= 64;
	static constexpr uint32_t g_simSize			= 128;
	static constexpr uint32_t g_qsmSize			= 512;

	enum class PeriphAddress
	{
		// SIM
		Syncr			= 0xFFA04,	// $YFFA04 CLOCK SYNTHESIZER CONTROL (SYNCR)
		PortE0			= 0xFFA11,	// $YFFA11 Port E Data Register
		PortE1			= 0xFFA13,	// $YFFA13 Port E Data Register
		DdrE			= 0xFFA15,	// $YFFA15 Port E Direction Register
		PEPar			= 0xFFA17,	// $YFFA17 Port E Pin Assignment Register
		PortF0			= 0xFFA19,	// $YFFA19 Port F Data Register
		PortF1			= 0xFFA1B,	// $YFFA1B Port F Data Register
		DdrF			= 0xFFA1D,	// $YFFA1D Port F Direction Register
		PFPar			= 0xFFA1F,	// $YFFA1F Port F Pin Assignment Register

		// QSM
		Qsmcr			= 0xffc00,
		Qtest			= 0xffc02,
		Qivr			= 0xffc04,		Qilr,
		NotUsedFFC06	= 0xffc06,
		SciControl0		= 0xffc08,
		SciControl1		= 0xffc0A,
		SciStatus		= 0xffc0C,
		SciData			= 0xffc0E,
		ReceiveRam0		= 0xffd00,
		TransmitRam0	= 0xffd20,
		CommandRam0		= 0xffd40,
	};
}
