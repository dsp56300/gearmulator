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

	// One-line mode is a single 80-cell run (0x00-0x4f) and advanceDdAddr() writes all of it,
	// but the display shift was reduced modulo 40, so the window could never start past cell
	// 39 and everything from 0x28 up was impossible to scroll into view.
	void testOneLineShiftReachesEveryCell()
	{
		Hd44780 lcd(20, 1);

		lcd.write(false, 0x30);				// Function set: 8 bit, one line
		lcd.write(false, 0x01);				// Clear display
		lcd.write(false, 0x80 | 0x45);		// Set DDRAM address 0x45
		lcd.write(true, 'Z');
		require(lcd.getDdRam()[0x45] == 'Z');

		// Shift the display left until 0x45 is the leftmost visible cell.
		for(uint32_t i = 0; i < 0x45; ++i)
			lcd.write(false, 0x18);			// Cursor/display shift: display, left

		require(lcd.getDisplayShift() == 0x45);
		require(lcd.getVisibleCharacter(0, 0) == 'Z');
	}

	// ...while two-line mode shifts each line over its own 40 cells, so a full lap is 40.
	void testTwoLineShiftWrapsAtOneLine()
	{
		Hd44780 lcd(20, 2);

		lcd.write(false, 0x38);				// Function set: 8 bit, two lines
		for(uint32_t i = 0; i < Hd44780::Columns; ++i)
			lcd.write(false, 0x18);

		require(lcd.getDisplayShift() == 0);
	}
}

int main()
{
	testLeavingCgRam(0x01);					// Clear display
	testLeavingCgRam(0x02);					// Return home
	testLeavingCgRam(0x03);					// Return home, don't-care bit set
	testLeavingCgRam(0x80);					// Set DDRAM address 0
	testUndefinedInstructionIsANop();
	testOneLineShiftReachesEveryCell();
	testTwoLineShiftWrapsAtOneLine();
	return 0;
}
