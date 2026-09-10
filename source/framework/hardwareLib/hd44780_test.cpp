#include "hd44780.h"

#include <cstdlib>

namespace
{
	using namespace hwLib;

	void require(const bool _condition)
	{
		if(!_condition)
			std::abort();
	}

	// Define a glyph, then take the controller back to DDRAM by every documented route.
	// Each of these loads the address counter with a DDRAM address, so each has to leave
	// the CGRAM mode the data register is in - otherwise the text lands in the glyph.
	void testLeavingCgRam(const uint8_t _instruction)
	{
		Hd44780 lcd;

		lcd.write(false, 0x40);				// Set CGRAM address 0
		lcd.write(true, 0x1f);				// one row of glyph 0

		lcd.write(false, _instruction);
		lcd.write(true, 'A');

		require(lcd.getCgRam()[0] == 0x1f);	// glyph survived
		require(lcd.getCgRam()[1] != 'A');	// text did not land in it
		require(lcd.getDdRam()[0] == 'A');	// it went to the display
	}

	// Instruction 0x00 is undefined on the real controller. It used to fall through the mask
	// chain into Set DDRAM address, which reset the counter and dropped out of CGRAM - so
	// firmware idling with it between Set CGRAM address and its data corrupted both.
	void testUndefinedInstructionIsANop()
	{
		Hd44780 lcd;

		lcd.write(false, 0x85);				// Set DDRAM address 5
		lcd.write(false, 0x00);
		require(lcd.getCursorAddress() == 0x05);

		lcd.write(false, 0x42);				// Set CGRAM address 2
		lcd.write(false, 0x00);
		lcd.write(true, 0x15);

		require(lcd.getCgRam()[2] == 0x15);
		require(lcd.getDdRam()[0] != 0x15);
	}
}

int main()
{
	testLeavingCgRam(0x01);					// Clear display
	testLeavingCgRam(0x02);					// Return home
	testLeavingCgRam(0x03);					// Return home, don't-care bit set
	testLeavingCgRam(0x80);					// Set DDRAM address 0
	testUndefinedInstructionIsANop();
	return 0;
}
