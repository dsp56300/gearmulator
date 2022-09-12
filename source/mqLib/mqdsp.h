#pragma once

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/peripherals.h"

namespace mqLib
{
	class MqDsp
	{
	public:
		static constexpr dsp56k::TWord g_bridgedAddr	= 0x080000;	// start of external SRAM, mapped to X and Y
		static constexpr dsp56k::TWord g_xyMemSize		= 0x800000;	// due to weird AAR mapping we just allocate enough so that everything fits into it
		static constexpr dsp56k::TWord g_pMemSize		= 0x2000;	// only $0000 < $1400 for DSP, rest for us
		static constexpr dsp56k::TWord g_bootCodeBase	= 0x1500;

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
		void dumpXYMem(const std::string& _filename) const;

	private:
		dsp56k::PeripheralsNop m_periphNop;
		dsp56k::Peripherals56362 m_periphX;
		dsp56k::Memory m_memory;
		dsp56k::DSP m_dsp;
		dsp56k::TWord m_memoryBuffer[dsp56k::Memory::calcMemSize(g_pMemSize, g_xyMemSize, g_bridgedAddr)];
	};
}
