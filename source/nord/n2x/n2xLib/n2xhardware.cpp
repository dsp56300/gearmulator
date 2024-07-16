#include "n2xhardware.h"

namespace n2x
{
	Hardware::Hardware()
		: m_uc(m_rom)
		, m_dspA(*this, m_uc.getHdi08A(), 0)
		, m_dspB(*this, m_uc.getHdi08B(), 1)
	{
		if(!m_rom.isValid())
			return;
	}

	bool Hardware::isValid() const
	{
		return m_rom.isValid();
	}

	void Hardware::process()
	{
		m_uc.exec();
	}
}
