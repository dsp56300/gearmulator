#pragma once

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/dspthread.h"
#include "dsp56kEmu/peripherals.h"

#include "dsp56kEmu/dspBootCode.h"

#include "wLib/wDsp.h"

#include <mutex>

namespace mc68k
{
	class Hdi08;
}

namespace mqLib
{
	// microQ firmware host command vectors (written to DSP via CVR by MC68K).
	// These dispatch as fast interrupts in the DSP's vector table.
	// All three reset R6 to N6 (buffer base address).
	enum class HostCommand : uint8_t
	{
		BatchStart   = 0x80,  // bclr #3,HCR  — clear HF2, begin new data batch
		BatchComplete= 0x82,  // move n6,y:$6  — signal commands pending for main loop
		SetHF2       = 0x92,  // bset #3,HCR  — set HF2 flag
	};

	class Hardware;

	class MqDsp : public wLib::Dsp
	{
	public:
		static constexpr dsp56k::TWord g_bridgedAddr	= 0x080000;	// start of external SRAM, mapped to X and Y
		static constexpr dsp56k::TWord g_xyMemSize		= 0x800000;	// due to weird AAR mapping we just allocate enough so that everything fits into it
		static constexpr dsp56k::TWord g_pMemSize		= 0x2000;	// only $0000 < $1400 for DSP, rest for us

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
		bool hasThread() const { return m_thread != nullptr; }

		bool receivedMagicEsaiPacket() const { return m_receivedMagicEsaiPacket; }
		bool isInBootMode() const { return m_inBootMode; }
		void onDspBootFinished();
		void reset();
		void terminateThread();
		void resetState();

		void dumpHdiLog() const;
		uint32_t getIndex() const { return m_index; }

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
		uint32_t m_hdiDspToUcCount = 0;

		std::unique_ptr<dsp56k::DSPThread> m_thread;

		bool m_receivedMagicEsaiPacket = false;
		uint32_t m_hdiTransferFailCount = 0;
		uint32_t m_hdiUcToDspCount = 0;
		bool m_commandProcessingActive = false;

		// Ring buffer of last 32 UC→DSP HDI08 words for crash diagnostics
		static constexpr uint32_t g_hdiLogSize = 32;
		dsp56k::TWord m_hdiUcToDspLog[g_hdiLogSize]{};
		uint32_t m_hdiUcToDspLogIndex = 0;

		// Protects m_boot and m_inBootMode from concurrent access between
		// the UC thread (invoking HDI08 callbacks) and the boot pump thread
		// (calling reset()). The callback is set once in the constructor and
		// never changed; the mutex ensures m_boot is not destroyed/reconstructed
		// while the UC thread is inside m_boot.hdiWriteTX().
		std::mutex m_hdiCallbackMutex;
		bool m_inBootMode = true;

		dsp56k::DspBoot m_boot;
	};
}
