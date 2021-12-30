#include "microcontrollerTypes.h"

#include <cassert>

namespace virusLib
{
	uint8_t toMidiByte(BankNumber _bank)
	{
		return static_cast<uint8_t>(_bank);
	}
	BankNumber fromMidiByte(uint8_t _byte)
	{
		return static_cast<BankNumber>(_byte);
	}
	uint32_t toArrayIndex(BankNumber _bank)
	{
		const auto bank = static_cast<uint8_t>(_bank);
		assert(bank > 0);
		return bank - 1;
	}

	BankNumber fromArrayIndex(uint8_t _bank)
	{
		return static_cast<BankNumber>(_bank + 1);
	}
}
