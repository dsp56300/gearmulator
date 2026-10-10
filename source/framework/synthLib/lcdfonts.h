#pragma once

#include <cstdint>

namespace synthLib
{
	// HD44780 character ROM, ten rows of five pixels per character
	const uint8_t* getCharacterData(uint8_t _character);
}
