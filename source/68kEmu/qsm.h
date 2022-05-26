#pragma once

#include "peripheralBase.h"
#include "peripheralTypes.h"

namespace mc68k
{
	class Qsm : public PeripheralBase
	{
	public:
		Qsm() : PeripheralBase(g_qsmBase, g_qsmSize) {}
	};
}
