#include "mqGui.h"

#include <iostream>

#include "../mqLib/mqhardware.h"

constexpr int g_deviceW = 140;
constexpr int g_deviceH = 16;

Gui::Gui(mqLib::Hardware& _hw)
	: m_hw(_hw)
	, m_win(g_deviceW, g_deviceH + 6)
	, m_lcd(_hw.getUC().getLcd())
	, m_leds(_hw.getUC().getLeds())
{
}

void Gui::render()
{
	handleTerminalSize();

	m_win.fill_fg(1,1,g_deviceW, 1, Term::fg::bright_yellow);
	m_win.fill_fg(1,g_deviceH,g_deviceW, g_deviceH, Term::fg::bright_yellow);

	for(int i=2; i<g_deviceH; ++i)
	{
		m_win.fill_fg(1,i,1,i, Term::fg::bright_yellow);
		m_win.fill_fg(g_deviceW,i,g_deviceW,i, Term::fg::bright_yellow);
	}

	m_win.print_rect(1,1,g_deviceW, g_deviceH);

	m_win.fill_fg(3, g_deviceH>>1, 9, g_deviceH>>1, Term::fg::bright_yellow);
	m_win.print_str(3, g_deviceH >> 1, "microQ");
	m_win.fill_fg((g_deviceW>>1) - 3, g_deviceH - 1, (g_deviceW>>1) + 7 , g_deviceH - 1, Term::fg::bright_blue);
	m_win.print_str((g_deviceW>>1) - 3, g_deviceH - 1, "waldorf");

	constexpr int lcdX = 10;

	renderAboveLCD(lcdX, 3);
	renderLCD(lcdX,6);
	renderBelowLCD(lcdX, 12);

	renderAlphaAndPlay(lcdX + 28, 7);

	renderMultimodePeek(lcdX + 23, g_deviceH - 2);
	renderVerticalLedsAndButtons(lcdX + 46, 3);
	renderCursorBlock(lcdX + 59, 6);

	constexpr int rightBase = 81;
	renderMatrixLEDs(rightBase, 3);
	renderMatrixText(rightBase + 18, 3);
	renderRightEncoders(rightBase + 20, g_deviceH - 4);

	renderButton(mqLib::Buttons::ButtonType::Power, g_deviceW - 6, g_deviceH - 2);
	renderLED(m_leds.getLedState(mqLib::Leds::Led::Power), g_deviceW - 4, g_deviceH - 3);
	renderLabel(g_deviceW - 2, g_deviceH - 1, "Power", true);

	renderHelp(2, g_deviceH + 2);
	renderDebug(2, g_deviceH + 5);

	std::cout << m_win.render(1,1,true) << std::flush;
}

void Gui::renderAboveLCD(int xStart, int yStart)
{
	constexpr int stepX = 5;
	constexpr int stepY = 2;
	int x = xStart + 3;
	renderLED(mqLib::Leds::Led::Inst1, x, yStart);	x += stepX;
	renderLED(mqLib::Leds::Led::Inst2, x, yStart);	x += stepX;
	renderLED(mqLib::Leds::Led::Inst3, x, yStart);	x += stepX;
	renderLED(mqLib::Leds::Led::Inst4, x, yStart);

	x = xStart + 3;
	auto y = yStart + stepY;
	renderButton(mqLib::Buttons::ButtonType::Inst1, x, y);	x += stepX;
	renderButton(mqLib::Buttons::ButtonType::Inst2, x, y);	x += stepX;
	renderButton(mqLib::Buttons::ButtonType::Inst3, x, y);	x += stepX;
	renderButton(mqLib::Buttons::ButtonType::Inst4, x, y);

	x = xStart + 4;
	y = yStart + 1;
	renderLabel(x, y, "1", false);	x += stepX;
	renderLabel(x, y, "2", false);	x += stepX;
	renderLabel(x, y, "3", false);	x += stepX;
	renderLabel(x, y, "4", false);
}

void Gui::renderLCD(int x, int y)
{
	m_win.print_rect(x+1, y+1, x+22, y+4,true);

	m_win.fill_bg(x+2, y+2, x+21, y+3, Term::bg::green);
	m_win.fill_fg(x+2, y+2, x+21, y+3, Term::fg::black);

	const auto& text = m_lcd.getDdRam();

	int cx = x + 2;
	int cy = y + 2;

	for(size_t i=0; i<text.size(); ++i)
	{
		// https://en.wikipedia.org/wiki/List_of_Unicode_characters

		char32_t c = static_cast<uint8_t>(text[i]);
		switch (c)
		{
		case 0:		c = 0x24EA;		break;
		case 1:		c = 0x2460;		break;
		case 2:		c = 0x2461;		break;
		case 3:		c = 0x2462;		break;
		case 4:		c = 0x2463;		break;
		case 5:		c = 0x2464;		break;
		case 6:		c = 0x2465;		break;
		case 7:		c = 0x2466;		break;
		case 0xdf:	c = 0x2598;		break;	// Quadrant upper left
		case 0xff:	c = 0x2588;		break;	// Full block
		default:
			if(c < 32 || c >= 0x0010FFFF)
				c = '?';
		}
		m_win.set_char(cx, cy, c);
		++cx;
		if(i == 19)
		{
			cx -= 20;
			++cy;
		}
	}
}

void Gui::renderBelowLCD(int x, int y)
{
	renderEncoder(mqLib::Buttons::Encoders::LcdLeft, x + 4, y);
	renderEncoder(mqLib::Buttons::Encoders::LcdRight, x + 14, y);
}

void Gui::renderAlphaAndPlay(int x, int y)
{
	renderEncoder(mqLib::Buttons::Encoders::Master, x, y);
	renderLED(mqLib::Leds::Led::Play, x + 8, y + 2);
	renderButton(mqLib::Buttons::ButtonType::Play, x + 6, y + 3);
	renderLabel(x+5, y+4, "Play", false);
}

void Gui::renderMultimodePeek(int x, int y)
{
	const auto xStart = x;
	renderLED(mqLib::Leds::Led::Multimode, x, y);
	x += 4;
	renderButton(mqLib::Buttons::ButtonType::Multimode, x, y);
	x += 6;
	renderLED(mqLib::Leds::Led::Peek, x, y);
	x += 4;
	renderButton(mqLib::Buttons::ButtonType::Peek, x, y);
	renderLabel(xStart - 2, y+1, "Multimode   Peek", false);
}

void Gui::renderCursorBlock(int x, int y)
{
	renderButton(mqLib::Buttons::ButtonType::Left, x, y + 2);
	renderButton(mqLib::Buttons::ButtonType::Right, x + 6, y + 2);
	renderButton(mqLib::Buttons::ButtonType::Up, x + 3, y);
	renderButton(mqLib::Buttons::ButtonType::Down, x + 3, y + 4);
}

void Gui::renderVerticalLedsAndButtons(int xStart, int y)
{
	constexpr int spreadX = 4;
	constexpr int stepY = 2;

	int x = xStart;
	renderLED(mqLib::Leds::Led::Global, x, y);	x += spreadX;
	renderButton(mqLib::Buttons::ButtonType::Global, x, y);
	x = xStart;
	renderLabel(x + 3, y + 1, "Global", true);
	renderLabel(x + 4, y + 1, "Util", false);
	y += stepY;
	renderLED(mqLib::Leds::Led::Multi, x, y);	x += spreadX;
	renderButton(mqLib::Buttons::ButtonType::Multi, x, y);
	x = xStart;
	renderLabel(x + 3, y + 1, "Multi", true);
	renderLabel(x + 4, y + 1, "Compare", false);
	y += stepY;
	renderLED(mqLib::Leds::Led::Edit, x, y);	x += spreadX;
	renderButton(mqLib::Buttons::ButtonType::Edit, x, y);
	x = xStart;
	renderLabel(x + 3, y + 1, "Edit", true);
	renderLabel(x + 4, y + 1, "Recall", false);
	y += stepY;
	renderLED(mqLib::Leds::Led::Sound, x, y);	x += spreadX;
	renderButton(mqLib::Buttons::ButtonType::Sound, x, y);
	x = xStart;
	renderLabel(x + 3, y + 1, "Sound", true);
	renderLabel(x + 4, y + 1, "Store", false);
	y += stepY;
	renderLED(mqLib::Leds::Led::Shift, x, y);	x += spreadX;
	renderButton(mqLib::Buttons::ButtonType::Shift, x, y);
	x = xStart;
	renderLabel(x + 1, y + 1, "Shift", false);
}

void Gui::renderMatrixLEDs(int xStart, int yStart)
{
	constexpr int stepX = 4;
	constexpr int stepY = 1;

	int x = xStart;
	int y = yStart;

	renderLED(mqLib::Leds::Led::Osc1, x, y);	x += stepX;
	renderLED(mqLib::Leds::Led::Osc2, x, y);	x += stepX;
	renderLED(mqLib::Leds::Led::Osc3, x, y);
	x = xStart;	y += stepY;
	renderLED(mqLib::Leds::Led::MixerRouting, x, y);
	x = xStart;	y += stepY;
	renderLED(mqLib::Leds::Led::Filters1, x, y);	x += stepX;
	renderLED(mqLib::Leds::Led::Filters2, x, y);
	x = xStart;	y += stepY;
	renderLED(mqLib::Leds::Led::AmpFx, x, y);
	x = xStart;	y += stepY;
	renderLED(mqLib::Leds::Led::Env1, x, y);	x += stepX;
	renderLED(mqLib::Leds::Led::Env2, x, y);	x += stepX;
	renderLED(mqLib::Leds::Led::Env3, x, y);	x += stepX;
	renderLED(mqLib::Leds::Led::Env4, x, y);
	x = xStart;	y += stepY;
	renderLED(mqLib::Leds::Led::LFOs, x, y);
	x = xStart;	y += stepY;
	renderLED(mqLib::Leds::Led::ModMatrix, x, y);
}

void Gui::renderMatrixText(int x, int y)
{
	m_win.fill_fg(x,y, x + 36, y + 7, Term::fg::gray);

	m_win.print_str(x, y, "Octave    Detune Shape     PWM      ");	++y;
	m_win.print_str(x, y, "Osc1      Osc2   Osc3      Filter   ");	++y;
	m_win.print_str(x, y, "Cutoff    Res    Env       Type     ");	++y;
	m_win.print_str(x, y, "Volume    FX Mix ArpMode   Tempo    ");	++y;
	m_win.print_str(x, y, "Attack    Decay  Sustain   Release  ");	++y;
	m_win.print_str(x, y, "LFO1Speed Shapee LFO2Speed LFO3Speed");	++y;
	m_win.print_str(x, y, "Select    Source Amount    Dest     ");
}

void Gui::renderRightEncoders(int x, int y)
{
	constexpr int xStep = 8;

	renderEncoder(mqLib::Buttons::Encoders::Matrix1, x, y);	x += xStep;
	renderEncoder(mqLib::Buttons::Encoders::Matrix2, x, y);	x += xStep;
	renderEncoder(mqLib::Buttons::Encoders::Matrix3, x, y);	x += xStep;
	renderEncoder(mqLib::Buttons::Encoders::Matrix4, x, y);
}

void Gui::renderHelp(int x, int y)
{
	renderLabel(x, y  , "Buttons: 1-4=Inst | Arrows=Cursor | g=Global | m=Multi | e=Edit | s=Sound | S=Shift | M=Multimode | p=Play | P=Peek | q=Power");
	renderLabel(x, y+1, "Encoders: F1/F2 & F3/F4=LCD Left/Right | 5/6=Alpha Dial | F5-F12=Matrix");
	renderLabel(x, y+2, "MIDI: 7=Note ON | 8=Note OFF | 9=Modwheel Max | 0=Modwheel Min");
	renderLabel(x, y+3, "Escape: Open Settings to select MIDI In/Out & Audio Out");
}

void Gui::renderDebug(int x, int y)
{
	renderLabel(g_deviceW - 1, y, std::string("    DSP ") + m_hw.getDspThread().getMipsString(), true, Term::fg::red);
}

void Gui::renderLED(mqLib::Leds::Led _led, int x, int y)
{
	renderLED(m_leds.getLedState(_led), x, y);
}

void Gui::renderLED(const bool on, int x, int y)
{
	if(on)
	{
		m_win.fill_fg(x, y, x+1, y, Term::fg::bright_white);
		m_win.fill_fg(x+1, y, x+1, y, Term::fg::bright_yellow);
		m_win.fill_fg(x+2, y, x+1, y, Term::fg::bright_white);
		m_win.print_str(x, y, "(*)");
	}
	else
	{
		m_win.fill_fg(x, y, x+1, y, Term::fg::white);
		m_win.fill_fg(x+1, y, x+1, y, Term::fg::white);
		m_win.fill_fg(x+2, y, x+1, y, Term::fg::white);
		m_win.print_str(x, y, "(-)");
	}
}

void Gui::renderButton(mqLib::Buttons::ButtonType _button, int x, int y)
{
	const auto pressed = m_hw.getUC().getButtons().getButtonState(_button);

	if(pressed)
	{
		m_win.print_str(x,y, "[#]");
	}
	else
	{
		m_win.print_str(x,y, "[ ]");
	}
}

void Gui::renderEncoder(mqLib::Buttons::Encoders _encoder, int x, int y)
{
	const auto state = m_hw.getUC().getButtons().getEncoderState(_encoder);
	constexpr char32_t overline = 0x0000203E;

	const char c0 = state & 2 ? '*' : '.';
	const char c1 = state & 1 ? '*' : '.';

	if(_encoder == mqLib::Buttons::Encoders::Master)
	{
		m_win.fill_fg(x, y, x + 5, y + 3, Term::fg::bright_red);
	}
	m_win.print_str(x, y,    " /  \\");
    m_win.print_str(x, y+1,  std::string("| ") + c0 + c1 + " |");
	m_win.print_str(x, y+2, " \\__/");

	m_win.set_char(x+2, y, overline);
	m_win.set_char(x+3, y, overline);
}

void Gui::renderLabel(int x, int y, const std::string& _text, bool _rightAlign, Term::fg _color/* = Term::fg::gray*/)
{
	const int len = static_cast<int>(_text.size());
	if(_rightAlign)
		x -= len;
	m_win.fill_fg(x, y, x + len - 1, y, _color);
	m_win.print_str(x,y,_text);
}
