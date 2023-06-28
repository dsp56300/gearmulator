#pragma once

#include <cpp-terminal/base.hpp>
#include <cpp-terminal/window.hpp>

#include "mqGuiBase.h"

#include "../mqLib/buttons.h"
#include "../mqLib/leds.h"

namespace mqLib
{
	class MicroQ;
}

namespace mqConsoleLib
{
	class Gui : public GuiBase
	{
	public:
		explicit Gui(mqLib::MicroQ& _mq);

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

		mqLib::MicroQ& m_mq;
		Term::Window m_win;
	};
}
