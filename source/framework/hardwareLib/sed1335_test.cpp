#include "sed1335.h"

#include <cstdlib>

namespace
{
	using namespace hwLib;

	void require(const bool _condition)
	{
		if(!_condition)
			std::abort();
	}

	SED1335 makePanel()
	{
		SED1335 lcd(160, 64, 0x8000);

		// SYSTEM SET, enough of it that the renderer has a sane geometry.
		lcd.writeCommand(0x40);
		const uint8_t systemSet[] = { 0x30, 0x87, 0x07, 0x27, 0x2f, 0x3f, 0x00, 0x00 };
		for(const auto b : systemSet)
			lcd.writeData(b);

		return lcd;
	}

	// CSRR reads the cursor address back, low byte then high. It used to answer with VRAM
	// at the cursor instead, handing the driver a garbage address to write to next.
	void testCursorReadBack()
	{
		auto lcd = makePanel();

		// Put something at the address we are about to park the cursor on, so returning
		// VRAM instead of the address cannot pass by accident.
		lcd.writeCommand(0x46);				// CSRW
		lcd.writeData(0x34);
		lcd.writeData(0x12);
		lcd.writeCommand(0x42);				// MWRITE - lands at 0x1234, advances to 0x1235
		lcd.writeData(0xa5);

		lcd.writeCommand(0x46);				// CSRW back to 0x1234
		lcd.writeData(0x34);
		lcd.writeData(0x12);

		lcd.writeCommand(0x47);				// CSRR
		require(lcd.readData() == 0x34);
		require(lcd.readData() == 0x12);
	}

	// CSRR must not move the cursor, and must not leave the write path armed for
	// parameter bytes that the command never receives.
	void testCursorReadBackDoesNotDisturbTheCursor()
	{
		auto lcd = makePanel();

		lcd.writeCommand(0x46);				// CSRW to 0x0100
		lcd.writeData(0x00);
		lcd.writeData(0x01);

		lcd.writeCommand(0x47);				// CSRR
		lcd.readData();
		lcd.readData();

		lcd.writeCommand(0x42);				// MWRITE must still land at 0x0100
		lcd.writeData(0x5a);

		lcd.writeCommand(0x47);
		require(lcd.readData() == 0x01);	// advanced by exactly one
		require(lcd.readData() == 0x01);
	}

	// A driver that double-buffers writes a page and flips to it with SCROLL. Only MWRITE and
	// DISP ON used to mark the panel dirty, so that flip notified nobody and the UI kept
	// showing the old page.
	void testDisplayCommandsNotify()
	{
		auto lcd = makePanel();

		int changes = 0;
		lcd.evChanged.addListener([&](auto&&...) { ++changes; });

		lcd.flush();						// drain whatever SYSTEM SET left behind
		changes = 0;

		// SCROLL: flips which VRAM region is displayed without touching a single pixel.
		lcd.writeCommand(0x44);
		for(int i = 0; i < 10; ++i)
			lcd.writeData(0x10);
		lcd.flush();
		require(changes == 1);

		// DISP OFF blanks the panel.
		lcd.writeCommand(0x58);
		lcd.writeData(0x00);
		lcd.flush();
		require(changes == 2);

		// HDOT SCR moves the whole image sideways.
		lcd.writeCommand(0x59);
		lcd.writeData(0x00);
		lcd.flush();
		changes = 0;
		lcd.writeCommand(0x5a);
		lcd.writeData(0x03);
		lcd.flush();
		require(changes == 1);

		// CGRAM ADR repoints the font, so every character on screen changes.
		lcd.writeCommand(0x5c);
		lcd.writeData(0x00);
		lcd.writeData(0x20);
		lcd.flush();
		require(changes == 2);

		// ...and a flush with nothing pending stays quiet.
		lcd.flush();
		require(changes == 2);
	}
}

int main()
{
	testCursorReadBack();
	testCursorReadBackDoesNotDisturbTheCursor();
	testDisplayCommandsNotify();
	return 0;
}
