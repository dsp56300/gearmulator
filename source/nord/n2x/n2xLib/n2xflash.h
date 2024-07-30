#pragma once

#include "hardwareLib/i2cFlash.h"

namespace n2x
{
	class Hardware;

	class Flash : public hwLib::I2cFlash
	{
	public:
		Flash(Hardware& _hardware);

	protected:
		uint8_t onReadByte() override;

	private:
		Hardware& m_hardware;
		uint32_t m_bootCounter = 2;
	};
}
