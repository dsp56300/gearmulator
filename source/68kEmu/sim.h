#pragma once

#include "peripheralBase.h"
#include "peripheralTypes.h"

namespace mc68k
{
	class Sim : public PeripheralBase
	{
	public:
		Sim() : PeripheralBase(g_simBase, g_simSize) {}

		uint16_t read16(PeriphAddress _addr) override;
		uint8_t read8(PeriphAddress _addr) override;

		void write8(PeriphAddress _addr, uint8_t _val) override;
	};
}
