#pragma once

#include "wLib/lcd.h"

namespace mc68k
{
	class Port;
}

namespace mqLib
{
	class LCD : public wLib::LCD
	{
	public:
		bool exec(mc68k::Port& _portGp, const mc68k::Port& _portF);

	private:
		uint32_t m_lastWriteCounter = 0xffffffff;
		uint32_t m_lastOpState = 0;
	};
}
