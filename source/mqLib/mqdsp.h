#pragma once

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/peripherals.h"

namespace mqLib
{
	class MqDsp
	{
	public:
		MqDsp();
		void exec();

		dsp56k::HDI08& hdi08()
		{
			return m_periphX.getHDI08();
		}

		dsp56k::DSP& dsp()
		{
			return m_dsp;
		}

		dsp56k::Peripherals56362& getPeriph()
		{
			return m_periphX;
		}

		void dumpPMem(const std::string& _filename);

	private:
		dsp56k::Peripherals56362 m_periphX;
		dsp56k::PeripheralsNop m_periphNop;
		dsp56k::Memory m_memory;
		dsp56k::DSP m_dsp;
	};
}
