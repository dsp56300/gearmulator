#pragma once

#include "common/device.hpp"
#include "hardwareLib/hd44780.h"

namespace jeLib
{
	class Lcd : public emu::Device, public hwLib::Hd44780
	{
	public:
		Lcd();

		void write8(uint32_t _addr, uint8_t _val) override;
		uint8_t read8(uint32_t _addr) override;
	};
}
