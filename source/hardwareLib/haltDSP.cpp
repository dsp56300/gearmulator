#include "haltDSP.h"

#include "dsp56kEmu/dsp.h"

namespace hwLib
{
	HaltDSP::HaltDSP(dsp56k::DSP& _dsp)
		: m_dsp(_dsp)
		, m_irq(_dsp.registerInterruptFunc([this] { onInterrupt(); }))
	{
	}

	void HaltDSP::haltDSP(const bool _wait)
	{
		if(++m_halted == 1)
		{
			++m_irqRequestCount;

			m_dsp.injectExternalInterrupt(m_irq);

			if(_wait)
			{
				const auto expectedCount = m_irqRequestCount;
				std::unique_lock lock(m_mutex);
				m_cvHalted.wait(lock, [this, expectedCount]
				{
					if(expectedCount != m_irqServedCount)
						return false;
					return true;
				});
			}
		}
	}

	bool HaltDSP::resumeDSP()
	{
		if(!m_halted)
			return false;
		if(--m_halted == 0)
			m_blockSem.notify();
		return true;
	}

	void HaltDSP::wakeUp(std::function<void()>&& _func)
	{
		if(!m_halted)
		{
			_func();
			return;
		}

		{
			std::unique_lock lock(m_mutex);
			m_wakeUps.emplace(m_wakeUpId++, std::move(_func));
		}
		m_blockSem.notify();
	}

	void HaltDSP::onInterrupt()
	{
		// signal waiter that we reached halt state
		{
			std::unique_lock lock(m_mutex);
			++m_irqServedCount;
		}
		m_cvHalted.notify_one();

		m_halting = true;

		// halt and wait for resume or a wakeup call
		m_blockSem.wait();

		while(true)
		{
			std::function<void()> func;

			// check if wakeup call
			{
				std::unique_lock lock(m_mutex);
				const auto it = m_wakeUps.find(m_wakeUpCount);
				if(it == m_wakeUps.end())
					break;

				func = std::move(it->second);
				m_wakeUps.erase(it); 
				++m_wakeUpCount;
			}

			// execute wakeup and go back to halt state
			func();
			m_blockSem.wait();
		}

		m_halting = false;
	}
}
