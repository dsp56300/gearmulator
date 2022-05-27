#pragma once

#include <deque>

#include "peripheralBase.h"
#include "peripheralTypes.h"
#include "qspi.h"

namespace mc68k
{
	class Mc68k;

	class Qsm : public PeripheralBase
	{
	public:
		Qsm(Mc68k& _mc68k);

		void write16(PeriphAddress _addr, uint16_t _val) override;
		uint16_t read16(PeriphAddress _addr) override;
		void write8(PeriphAddress _addr, uint8_t _val) override;
		uint8_t read8(PeriphAddress _addr) override;

		void exec() override;

		uint16_t spcr0()		{ return read16(PeriphAddress::Spcr0); }
		uint16_t spcr1()		{ return read16(PeriphAddress::Spcr1); }
		uint16_t spcr2()		{ return read16(PeriphAddress::Spcr2); }
		uint16_t spcr3()		{ return read16(PeriphAddress::Spcr3); }
		uint8_t spsr()			{ return read8(PeriphAddress::Spsr); }

		void spsr(uint8_t _value)	{ write8(PeriphAddress::Spsr, _value); }
		void spcr1(uint16_t _value)	{ write16(PeriphAddress::Spcr1, _value); }

	private:
		void startTransmit();
		void finishTransfer();
		void execTransmit();

		static PeriphAddress transmitRamAddr(uint8_t _offset);

		Mc68k& m_mc68k;

		Qspi m_qspi;
		uint8_t m_nextQueue = 0xff;
		std::deque<uint16_t> m_txData;
	};
}
