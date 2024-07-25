#include "haltDSP.h"

#include "dsp56kEmu/dsp.h"

namespace hwLib
{
	HaltDSP::HaltDSP(dsp56k::DSP& _dsp)
		: m_dsp(_dsp)
		, m_irq(_dsp.registerInterruptFunc([this] { onInterrupt(); }))
	{
	}

	void HaltDSP::haltDSP()
	{
		if(m_halted)
			return;
		if(++m_halted == 1)
			m_dsp.injectExternalInterrupt(m_irq);
	}

	bool HaltDSP::resumeDSP()
	{
		if(!m_halted)
			return false;
		if(--m_halted == 0)
			m_semaphore.notify();
		return true;
	}

	void HaltDSP::onInterrupt()
	{
		m_semaphore.wait();
	}
}
