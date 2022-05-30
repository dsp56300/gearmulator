#pragma once

#include "peripheralBase.h"
#include "peripheralTypes.h"
#include "port.h"

namespace mc68k
{
	class Gpt : public PeripheralBase
	{
	public:
		Gpt() : PeripheralBase(g_gptBase, g_gptSize) {}

		void write8(PeriphAddress _addr, uint8_t _val) override;
		uint8_t read8(PeriphAddress _addr) override;
		void write16(PeriphAddress _addr, uint16_t _val) override;
		uint16_t read16(PeriphAddress _addr) override;

		Port& getPortGP() { return m_portGP; }

	private:
		Port m_portGP;
	};
}
