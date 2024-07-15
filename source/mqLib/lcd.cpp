#include "lcd.h"

#include "mqtypes.h"

#include "mc68k/port.h"

namespace mqLib
{
	bool LCD::exec(mc68k::Port& _portGp, const mc68k::Port& _portF)
	{
		if(_portF.getWriteCounter() == m_lastWriteCounter)
			return false;

		m_lastWriteCounter = _portF.getWriteCounter();

		const auto f = _portF.read();
		const auto g = _portGp.read();
		const auto df = _portF.getDirection();
		//const auto dg = _portGp.getDirection();

		const auto registerSelect = (f>>LcdRS) & 1;
		const auto read = (f>>LcdRW) & 1;
		const auto opEnable = ((f & df)>>LcdLatch) & 1;

		// falling edge triggered
		const auto execute = m_lastOpState && !opEnable;

		m_lastOpState = opEnable;

		if(!execute)
			return false;

		const auto res = wLib::LCD::exec(registerSelect, read, g);

		if(res)
			_portGp.writeRX(*res);

		return true;
	}
}
