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

	private:
		Rom m_rom;
		Microcontroller m_uc;
		DSP m_dspA;
		DSP m_dspB;
	};
}
