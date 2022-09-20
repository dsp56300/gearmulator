#pragma once

#include <deque>

#include "peripheralBase.h"
#include "peripheralTypes.h"
#include "qspi.h"
#include "port.h"

namespace mc68k
{
	class Mc68k;

	class Qsm final : public PeripheralBase<g_qsmBase, g_qsmSize>
	{
	public:
		enum class Sccr1Bits
		{
			SendBreak,
			ReceiverWakeup,
			ReceiverEnable,
			TransmitterEnable,
			IdleLineInterruptEnable,
			ReceiverInterruptEnable,
			TransmitCompleteInterruptEnable,
			TransmitInterruptEnable,
			WakeupByAddressMark,
			ModeSelect,
			ParityEnable,
			ParityType,
			IdleLineDetectType,
			WiredOrModeForSciPins,
			LoopMode,
		};

		enum class ScsrBits
		{
			ParityError,
			FramingError,
			NoiseError,
			OverrunError,
			IdleLineDetected,
			ReceiverActive,
			ReceiveDataRegisterFull,
			TransmitComplete,
			TransmitDataRegisterEmpty
		};

		Qsm(Mc68k& _mc68k);

		void write16(PeriphAddress _addr, uint16_t _val) override;
		uint16_t read16(PeriphAddress _addr) override;
		void write8(PeriphAddress _addr, uint8_t _val) override;
		uint8_t read8(PeriphAddress _addr) override;

		void injectInterrupt(ScsrBits _scsrBits);
		void exec(uint32_t _deltaCycles) override;

		uint16_t spcr0()			{ return PeripheralBase::read16(PeriphAddress::Spcr0); }
		uint16_t spcr1()			{ return PeripheralBase::read16(PeriphAddress::Spcr1); }
		uint16_t spcr2()			{ return PeripheralBase::read16(PeriphAddress::Spcr2); }
		uint16_t spcr3()			{ return PeripheralBase::read16(PeriphAddress::Spcr3); }
		uint8_t spsr()				{ return PeripheralBase::read8(PeriphAddress::Spsr); }

		void spcr0(uint16_t _value)	{ PeripheralBase::write16(PeriphAddress::Spcr0, _value); }
		void spcr1(uint16_t _value)	{ PeripheralBase::write16(PeriphAddress::Spcr1, _value); }
		void spcr2(uint16_t _value)	{ PeripheralBase::write16(PeriphAddress::Spcr2, _value); }
		void spcr3(uint16_t _value)	{ PeripheralBase::write16(PeriphAddress::Spcr3, _value); }
		void spsr(uint8_t _value)	{ PeripheralBase::write8(PeriphAddress::Spsr, _value); }

		void writeSciRX(uint16_t _data);
		void readSciTX(std::deque<uint16_t>& _dst);

		Port& getPortQS() { return m_portQS; }

	private:
		void startTransmit(bool _startAtZero = false);
		void finishTransfer();
		void execTransmit();
		void cancelTransmit();
		static uint16_t bitTest(uint16_t _value, Sccr1Bits _bit);
		uint16_t bitTest(Sccr1Bits _bit);
		uint16_t bitTest(ScsrBits _bit);
		void clear(ScsrBits _bit);
		void set(ScsrBits _bit);
		uint16_t readSciRX();

		static PeriphAddress transmitRamAddr(uint8_t _offset);

		void writeSciData(uint16_t _data);
		uint16_t readSciStatus();

		Mc68k& m_mc68k;

		Port m_portQS;
		Qspi m_qspi;
		uint8_t m_nextQueue = 0xff;
		uint32_t m_spiDelay = 0;
		std::deque<uint16_t> m_spiTxData;
		std::deque<uint16_t> m_sciTxData;
		std::deque<uint16_t> m_sciRxData;
		uint16_t m_pendingTxDataCounter = 0;
	};
}
