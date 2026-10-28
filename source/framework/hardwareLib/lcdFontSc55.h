#pragma once

#include <cstdint>

namespace hwLib
{
	// The LCD character generator of the SC-55, as Nuked-SC55 draws it: characters 0x10 to 0xff, ten rows of five
	// pixels each, the top row first and the leftmost pixel in bit 4. Characters 0-7 are the display's CGRAM
	extern const uint8_t g_lcdFontSc55[240][10];
}
