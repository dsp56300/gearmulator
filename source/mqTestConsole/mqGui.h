#pragma once

#include <cpp-terminal/base.hpp>
#include <cpp-terminal/window.hpp>

#include "../mqLib/buttons.h"
#include "../mqLib/leds.h"

namespace mqLib
{
	class Buttons;
	class Leds;
	class LCD;
	class Hardware;
}

class Gui
{
public:
	explicit Gui(mqLib::Hardware& _hw);

	void render();

private:
	void renderAboveLCD(int x, int y);
	void renderLCD(int x, int y);
	void renderBelowLCD(int x, int y);
	void renderAlphaAndPlay(int x, int y);
	void renderMultimodePeek(int x, int y);
	void renderCursorBlock(int x, int y);
	void renderVerticalLedsAndButtons(int x, int y);
	void renderMatrixLEDs(int x, int y);
	void renderMatrixText(int x, int y);
	void renderRightEncoders(int x, int y);
	void renderHelp(int x, int y);
	void renderDebug(int x, int y);

	void renderLED(mqLib::Leds::Led _led, int x, int y);
	void renderLED(bool _on, int x, int y);
	void renderButton(mqLib::Buttons::ButtonType _button, int x, int y);
	void renderEncoder(mqLib::Buttons::Encoders _encoder, int x, int y);
	void renderLabel(int x, int y, const std::string& _text, bool _rightAlign = false, Term::fg _color = Term::fg::gray);

	mqLib::Hardware& m_hw;
	Term::Window m_win;
	mqLib::LCD& m_lcd;
	mqLib::Leds& m_leds;
};
