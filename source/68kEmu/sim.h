#pragma once

#include "peripheralBase.h"
#include "peripheralTypes.h"
#include "port.h"

namespace mc68k
{
	class Mc68k;

	class Sim final : public PeripheralBase<g_simBase, g_simSize>
	{
	public:
		enum PicrBits : uint16_t
		{
			PivMask		= 0xff,		// Periodic Interrupt Vector
			PirqlMask	= 0x700,	// Periodic Interrupt Request Level
			PirqlShift  = 8,
		};

		enum PitrBits : uint16_t
		{
			Pitm = 0xff,		// Periodic Interrupt Timing Modulus
			Ptp = (1<<8)		// Periodic Timer Prescaler Control, 0 = off, 1 = prescaled by 512
		};

		explicit Sim(Mc68k& _mc68k);

		uint16_t read16(PeriphAddress _addr) override;
		uint8_t read8(PeriphAddress _addr) override;

		void write8(PeriphAddress _addr, uint8_t _val) override;
		void write16(PeriphAddress _addr, uint16_t _val) override;

		void exec(uint32_t _deltaCycles) override;

		Port& getPortE() { return m_portE; }
		Port& getPortF() { return m_portF; }

		uint32_t getSystemClockHz() const { return m_systemClockHz; }

	private:
		void initTimer();
		void updateClock();

		Mc68k& m_mc68k;
		int32_t m_timerLoadValue = 0;
		int32_t m_timerCurrentValue = 0;
		Port m_portE, m_portF;
		uint32_t m_externalClockHz = 32768;
		uint32_t m_systemClockHz = 0;
	};
}
