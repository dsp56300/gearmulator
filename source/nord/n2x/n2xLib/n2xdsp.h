#pragma once

#include <memory>
#include <string>

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/dspthread.h"

#include "baseLib/trigger.h"

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

		void setEsaiCallback(std::function<void()>&& _func)
		{
			m_esaiCallback = std::move(_func);
		}

		void advanceSamples(uint32_t _samples, uint32_t _latency);

		dsp56k::DSPThread& getDSPThread() const { return *m_thread.get(); }

	private:
		void onUCRxEmpty(bool _needMoreData);
		void hdiTransferUCtoDSP(uint32_t _word);
		void hdiSendIrqToDSP(uint8_t _irq);
		uint8_t hdiUcReadIsr(uint8_t _isr);
		void onEsaiCallback();
		bool hdiTransferDSPtoUC();
		void waitDspRxEmpty();

		Hardware& m_hardware;
		mc68k::Hdi08& m_hdiUC;

		const uint32_t m_index;
		const std::string m_name;

		dsp56k::PeripheralsNop m_periphNop;
		dsp56k::Peripherals56362 m_periphX;
		dsp56k::Memory m_memory;
		dsp56k::DSP m_dsp;

		std::unique_ptr<dsp56k::DSPThread> m_thread;

		bool m_receivedMagicEsaiPacket = false;
		uint32_t m_hdiHF01 = 0;

		uint64_t m_numEsaiCallbacks = 0;
		uint64_t m_maxEsaiCallbacks = 0;
		uint64_t m_esaiLatency = 0;

		std::function<void()> m_esaiCallback = [] {};

		std::condition_variable m_haltDSPcv;
		std::mutex m_haltDSPmutex;

		baseLib::Trigger<> m_triggerInterruptDone;
		uint32_t m_vbaInterruptDone = 0;
	};
}
