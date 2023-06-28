#pragma once

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/dspthread.h"
#include "dsp56kEmu/peripherals.h"

namespace mc68k
{
	class Hdi08;
}

namespace mqLib
{
	class Hardware;

	class MqDsp
	{
	public:
		static constexpr dsp56k::TWord g_bridgedAddr	= 0x080000;	// start of external SRAM, mapped to X and Y
		static constexpr dsp56k::TWord g_xyMemSize		= 0x800000;	// due to weird AAR mapping we just allocate enough so that everything fits into it
		static constexpr dsp56k::TWord g_pMemSize		= 0x2000;	// only $0000 < $1400 for DSP, rest for us
		static constexpr dsp56k::TWord g_bootCodeBase	= 0x1500;

		MqDsp(Hardware& _hardware, mc68k::Hdi08& _hdiUC, uint32_t _index);
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

		void transferHostFlagsUc2Dsdp();
		bool hdiTransferDSPtoUC();
		void hdiSendIrqToDSP(uint8_t _irq);

		dsp56k::DSPThread& thread() { return *m_thread; }
		bool haveSentTXToDSP() const { return m_haveSentTXtoDSP; }

		bool receivedMagicEsaiPacket() const { return m_receivedMagicEsaiPacket; }

	private:
		void onUCRxEmpty(bool _needMoreData);
		void hdiTransferUCtoDSP(dsp56k::TWord _word);
		uint8_t hdiUcReadIsr(uint8_t _isr);
		void waitDspRxEmpty();

		Hardware& m_hardware;
		mc68k::Hdi08& m_hdiUC;
		const uint32_t m_index;
		const std::string m_name;

		dsp56k::PeripheralsNop m_periphNop;
		dsp56k::Peripherals56362 m_periphX;
		dsp56k::Memory m_memory;
		dsp56k::DSP m_dsp;
		dsp56k::TWord m_memoryBuffer[dsp56k::Memory::calcMemSize(g_pMemSize, g_xyMemSize, g_bridgedAddr)]{0};

		bool m_haveSentTXtoDSP = false;
		uint32_t m_hdiHF01 = 0;	// uc => DSP

		std::unique_ptr<dsp56k::DSPThread> m_thread;

		bool m_receivedMagicEsaiPacket = false;
	};
}
