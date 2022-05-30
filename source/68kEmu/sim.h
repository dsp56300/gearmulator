#pragma once

#include "peripheralBase.h"
#include "peripheralTypes.h"
#include "port.h"

namespace mc68k
{
	class Sim : public PeripheralBase
	{
	public:
		Sim() : PeripheralBase(g_simBase, g_simSize) {}

		uint16_t read16(PeriphAddress _addr) override;
		uint8_t read8(PeriphAddress _addr) override;

		void write8(PeriphAddress _addr, uint8_t _val) override;

		Port& getPortE() { return m_portE; }
		Port& getPortF() { return m_portF; }

	private:
		Port m_portE, m_portF;
	};
}
