#pragma once

#include <vector>

#include "Moira.h"

namespace mc68k
{
	class Mc68k : public moira::Moira
	{
	public:
		Mc68k();
		~Mc68k() override;

		static void writeW(std::vector<uint8_t>& _buf, size_t _offset, uint16_t _value);
		static uint16_t readW(const std::vector<uint8_t>& _buf, size_t _offset);
	};
}
