#include "xtLcd.h"

namespace xt
{
	Lcd::Lcd()
	{
		m_lcdData.fill(' ');
	}

	void Lcd::resetWritePos()
	{
		m_lcdWritePos = 0;
	}

	bool Lcd::writeCharacter(const char _c)
	{
		auto& c = m_lcdData[m_lcdWritePos];

		// Like the display controller, continue at the start of line one after the end of line two. The firmware starts
		// every refresh at address 0, but a refresh whose address command is lost writes on.
		if(++m_lcdWritePos == m_lcdData.size())
			m_lcdWritePos = 0;

		if(c == _c)
			return false;
		c = _c;
		return true;
	}

	std::string Lcd::toString() const
	{
		const std::string lineA(m_lcdData.data(), 40);
		const std::string lineB(m_lcdData.data() + 40, 40);
		return lineA + '\n' + lineB;
	}
}
