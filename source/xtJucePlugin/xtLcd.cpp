#include "xtLcd.h"

#include "version.h"

#include "../wLib/lcdfonts.h"

// 40*2 LCD simulation

XtLcd::XtLcd(Component& _parent) : Lcd(_parent, 40, 2)
{
	postConstruct();
}

XtLcd::~XtLcd() = default;

void XtLcd::setText(const std::array<uint8_t, 80>& _text)
{
	const std::vector<uint8_t> text{_text.begin(), _text.end()};
	Lcd::setText(text);
}

bool XtLcd::getOverrideText(std::vector<std::vector<uint8_t>>& _lines)
{
	const std::string lineA(std::string("Vavra v") + g_pluginVersionString);
	const std::string lineB = __DATE__ " " __TIME__;

	_lines = 
	{
		std::vector<uint8_t>(lineA.begin(), lineA.end()),
		std::vector<uint8_t>(lineB.begin(), lineB.end())
	};

	return true;
}

const uint8_t* XtLcd::getCharacterData(const uint8_t _character) const
{
	return wLib::getCharacterData(_character);
}
