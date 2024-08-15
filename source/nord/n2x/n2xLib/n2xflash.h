#pragma once

#include "hardwareLib/i2cFlash.h"

namespace n2x
{
	class Hardware;

	class Flash : public hwLib::I2cFlash
	{
	public:
		Flash(Hardware& _hardware);

	private:
		Hardware& m_hardware;
	};
}
