#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>

#include "baseLib/semaphore.h"

namespace dsp56k
{
	class DSP;
}

namespace hwLib
{
	class HaltDSP
	{
	public:
		explicit HaltDSP(dsp56k::DSP& _dsp);

		void haltDSP(bool _wait = false);
		bool resumeDSP();

		// only returns true if the last halt request has been serviced
		bool isHalting() const { return m_halting && m_irqRequestCount == m_irqServedCount; }

		dsp56k::DSP& getDSP() const { return m_dsp; }

		void wakeUp(std::function<void()>&& _func);

	private:
		void onInterrupt();

		dsp56k::DSP& m_dsp;

		uint32_t m_halted = 0;
		uint32_t m_irq;
		baseLib::Semaphore m_blockSem;

		std::mutex m_mutex;
		std::condition_variable m_cvHalted;

		uint32_t m_irqServedCount = 0;
		uint32_t m_irqRequestCount = 0;

		uint32_t m_wakeUpId = 0;
		uint32_t m_wakeUpCount = 0;

		std::unordered_map<uint32_t, std::function<void()>> m_wakeUps;

		bool m_halting = false;
	};

	class ScopedResumeDSP
	{
	public:
		explicit ScopedResumeDSP(HaltDSP& _haltDSP) : m_haltDSP(_haltDSP), m_resumed(_haltDSP.resumeDSP())
		{
		}

		~ScopedResumeDSP()
		{
			if(m_resumed)
				m_haltDSP.haltDSP();
		}
	private:
		HaltDSP& m_haltDSP;
		bool m_resumed;
	};

	class ScopedHaltDSP
	{
	public:
		explicit ScopedHaltDSP(HaltDSP& _haltDSP) : m_haltDSP(_haltDSP)
		{
			_haltDSP.haltDSP();
		}

		~ScopedHaltDSP()
		{
			m_haltDSP.resumeDSP();
		}
	private:
		HaltDSP& m_haltDSP;
	};
}
