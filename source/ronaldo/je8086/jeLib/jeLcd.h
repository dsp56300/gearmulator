#pragma once

#include <h8s/h8s.hpp>

#include "hardwareLib/lcd.h"

namespace jeLib
{
	class Lcd : public H8SDevice, public hwLib::LCD
	{
	public:
		Lcd();

		void write(uint32_t _addr, uint8_t _val) override;
		uint8_t read(uint32_t _addr) override;
	};
}
