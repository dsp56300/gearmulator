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
		if(m_lcdData[m_lcdWritePos] == _c)
		{
			++m_lcdWritePos;
			return false;
		}
		m_lcdData[m_lcdWritePos++] = _c;
		return true;
	}

	std::string Lcd::toString() const
	{
		const std::string lineA(m_lcdData.data(), 40);
		const std::string lineB(m_lcdData.data() + 40, 40);
		return lineA + '\n' + lineB;
	}
}
