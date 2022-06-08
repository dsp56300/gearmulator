#pragma once

#include "peripheralBase.h"
#include "peripheralTypes.h"
#include "port.h"

namespace mc68k
{
	class Mc68k;

	class Gpt : public PeripheralBase
	{
	public:
		Gpt(Mc68k& _mc68k) : PeripheralBase(g_gptBase, g_gptSize), m_mc68k(_mc68k) {}

		void write8(PeriphAddress _addr, uint8_t _val) override;
		uint8_t read8(PeriphAddress _addr) override;
		void write16(PeriphAddress _addr, uint16_t _val) override;
		uint16_t read16(PeriphAddress _addr) override;

		Port& getPortGP() { return m_portGP; }

	private:
		Mc68k& m_mc68k;
		Port m_portGP;
	};
}
