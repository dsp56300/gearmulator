#pragma once

#include "hardwareLib/i2cFlash.h"

namespace n2x
{
	class Flash : public hwLib::I2cFlash
	{
	public:
		Flash();
	};
}
