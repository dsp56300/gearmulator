#pragma once

#include <cstdint>

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

		void haltDSP();
		bool resumeDSP();

	private:
		void onInterrupt();

		dsp56k::DSP& m_dsp;

		uint32_t m_halted = 0;
		uint32_t m_irq;
		baseLib::Semaphore m_semaphore;
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
