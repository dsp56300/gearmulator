#pragma once

#include "peripheralBase.h"
#include "peripheralTypes.h"

namespace mc68k
{
	class Gpt : public PeripheralBase
	{
	public:
		Gpt() : PeripheralBase(g_gptBase, g_gptSize) {}
	};
}
