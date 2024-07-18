#pragma once
#include "n2xdsp.h"
#include "n2xmc.h"
#include "n2xrom.h"

namespace n2x
{
	class Hardware
	{
	public:
		Hardware();

		bool isValid() const;

		void process();

		Microcontroller& getUC() {return m_uc; }
		void ucYieldLoop(const std::function<bool()>& _continue);

	private:
		Rom m_rom;
		Microcontroller m_uc;
		DSP m_dspA;
		DSP m_dspB;
	};
}
