#pragma once

#include <cstdint>

namespace mc68k
{
	static constexpr uint32_t g_peripheralMask	= 0xfffff;

	static constexpr uint32_t g_hdi08Base		= 0xfd000;
	static constexpr uint32_t g_gptBase			= 0xff900;
	static constexpr uint32_t g_simBase			= 0xffa00;
	static constexpr uint32_t g_qsmBase			= 0xffc00;

	static constexpr uint32_t g_hdi08Size		= 8;
	static constexpr uint32_t g_gptSize			= 64;
	static constexpr uint32_t g_simSize			= 128;
	static constexpr uint32_t g_qsmSize			= 512;

	enum class PeriphAddress
	{
		// HDI08
		HdiICR			= 0xfd000,	// Interface Control Register (ICR)
		HdiCVR			= 0xfd001,	// Command Vector Register (CVR)
		HdiISR			= 0xfd002,	// Interface Status Register (ISR)
		HdiIVR			= 0xfd003,	// Interrupt Vector Register (IVR)
		HdiUnused4		= 0xfd004,
		HdiTXH			= 0xfd005,	// Receive Byte Registers (RXH:RXM:RXL)
		HdiTXM			= 0xfd006,	//   or Transmit Byte Registers (TXH:TXM:TXL)
		HdiTXL			= 0xfd007,	//   byte order depends on HLEND endianess setting

		// GPT
		DdrGp			= 0xFF906,	// Port GP Data Direction Register $YFF906
		PortGp			= 0xFF907,	// Port GP Data Register $YFF907
		Tcnt			= 0xFF90a,	// Timer Counter
		TcntLSB			= 0xFF90b,

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
		Picr			= 0xFFA22,	// $YFFA22 Periodic Interrupt Control Register
		Pitr			= 0xFFA24,	// $YFFA24 Periodic Interrupt Timer Register

		// QSM
		Qsmcr			= 0xffc00,	// $YFFC00 QSM Configuration Register
		Qtest			= 0xffc02,	// $YFFC02 QSM Test Register
		Qilr			= 0xffc04,	// $YFFC04 QSM Interrupt Level Register
		Qivr,						// $YFFC05 QSM Interrupt Vector Register
		NotUsedFFC06	= 0xffc06,
		SciControl0		= 0xffc08,	// $YFFC08 SCI Control Register 0
		SciControl1		= 0xffc0a,	// $YFFC0A SCI Control Register 1
		SciControl1LSB	= 0xffc0b,
		SciStatus		= 0xffc0c,	// $YFFC0C SCI Status Register
		SciData			= 0xffc0e,	// $YFFC0E SCI Data Register
		SciDataLSB		= 0xffc0f,

		Portqs			= 0xffc15,	// $YFFC15 Port QS Data Register
		Pqspar			= 0xffc16,	// $YFFC16 PORT QS Pin Assignment Register
		Ddrqs			= 0xffc17,	// $YFFC17 PORT QS Data Direction Register
		Spcr0			= 0xFFC18,	// $YFFC18 QSPI Control Register 0
		Spcr1			= 0xFFC1a,	// $YFFC1A QSPI Control Register 1
		Spcr2			= 0xFFC1c,	// $YFFC1C QSPI Control Register 2
		Spcr3			= 0xFFC1e,	// $YFFC1E QSPI Control Register 3
		Spsr			= 0xFFC1f,	// $YFFC1F QSPI Status Register

		ReceiveRam0		= 0xffd00,
		TransmitRam0	= 0xffd20,
		CommandRam0		= 0xffd40,
	};
}
