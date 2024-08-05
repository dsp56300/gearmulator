#pragma once

#include <memory>
#include <string>

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/dspthread.h"

#include "baseLib/semaphore.h"

#include "hardwareLib/dspBootCode.h"
#include "hardwareLib/haltDSP.h"

namespace mc68k
{
	class Hdi08;
}

namespace n2x
{
	class Hardware;

	class DSP
	{
	public:
		DSP(Hardware& _hw, mc68k::Hdi08& _hdiUc, uint32_t _index);

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

		dsp56k::DSPThread& getDSPThread() const { return *m_thread; }
		auto& getHaltDSP() { return m_haltDSP; }

		void terminate();
		void join() const;
		void onDspBootFinished();

	private:
		void onUCRxEmpty(bool _needMoreData);
		void hdiTransferUCtoDSP(uint32_t _word);
		void hdiSendIrqToDSP(uint8_t _irq);
		uint8_t hdiUcReadIsr(uint8_t _isr);
		bool hdiTransferDSPtoUC();

		Hardware& m_hardware;
		mc68k::Hdi08& m_hdiUC;

		const uint32_t m_index;
		const std::string m_name;

		dsp56k::PeripheralsNop m_periphNop;
		dsp56k::Peripherals56362 m_periphX;
		dsp56k::Memory m_memory;
		dsp56k::DSP m_dsp;

		std::unique_ptr<dsp56k::DSPThread> m_thread;

		baseLib::Semaphore m_triggerInterruptDone;
		uint32_t m_irqInterruptDone = 0;

		hwLib::HaltDSP m_haltDSP;

		hwLib::DspBoot m_boot;
	};
}
