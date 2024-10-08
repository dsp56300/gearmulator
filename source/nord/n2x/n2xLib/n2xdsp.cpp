#include "n2xdsp.h"

#include "n2xhardware.h"

#include "dsp56kDebugger/debugger.h"

#include "dsp56kEmu/dspthread.h"

#include "mc68k/hdi08.h"

namespace n2x
{
	static constexpr dsp56k::TWord g_xyMemSize			= 0x010000;
	static constexpr dsp56k::TWord g_externalMemAddr	= 0x008000;
	static constexpr dsp56k::TWord g_pMemSize			= 0x004000;

	namespace
	{
		dsp56k::DefaultMemoryValidator g_memValidator;
	}
	DSP::DSP(Hardware& _hw, mc68k::Hdi08& _hdiUc, const uint32_t _index)
	: m_hardware(_hw)
	, m_hdiUC(_hdiUc)
	, m_index(_index)
	, m_name(_index ? "DSP B" : "DSP A")
	, m_memory(g_memValidator, g_pMemSize, g_xyMemSize, g_externalMemAddr)
	, m_dsp(m_memory, &m_periphX, &m_periphNop)
	, m_haltDSP(m_dsp)
	, m_boot(m_dsp)
	{
		if(!_hw.isValid())
			return;

		{
			auto& clock = m_periphX.getEsaiClock();
			auto& esai = m_periphX.getEsai();

			clock.setExternalClockFrequency(3'333'333); // schematic claims 1 MHz but we measured 10/3 Mhz

			constexpr auto samplerate = g_samplerate;
			constexpr auto clockMultiplier = 2;

			clock.setSamplerate(samplerate * clockMultiplier);

			clock.setClockSource(dsp56k::EsaiClock::ClockSource::Cycles);

			if(m_index == 0)
			{
				// DSP A = chip U2 = left on the schematic
				// Sends its audio to DSP B at twice the sample rate, it sends four words per frame
				clock.setEsaiDivider(&esai, 0);
			}
			else
			{
				// DSP B = chip U3 = right on the schematic
				// receives audio from DSP A at twice the sample rate
				// sends its audio to the DACs at regular sample rate
				clock.setEsaiDivider(&esai, 1, 0);
//				clock.setEsaiCounter(&esai, -1, 0);
			}
		}

		auto config = m_dsp.getJit().getConfig();

		config.aguSupportBitreverse = true;
		config.linkJitBlocks = true;
		config.dynamicPeripheralAddressing = false;
#ifdef _DEBUG
		config.debugDynamicPeripheralAddressing = true;
#endif
		config.maxInstructionsPerBlock = 0;
		config.support16BitSCMode = true;
		config.dynamicFastInterrupts = true;

		m_dsp.getJit().setConfig(config);

		// fill P memory with something that reminds us if we jump to garbage
		for(dsp56k::TWord i=0; i<m_memory.sizeP(); ++i)
		{
			m_memory.set(dsp56k::MemArea_P, i, 0x000200);	// debug instruction
			m_dsp.getJit().notifyProgramMemWrite(i);
		}

		hdi08().setRXRateLimit(0);

		m_periphX.getEsai().writeEmptyAudioIn(2);

		m_hdiUC.setRxEmptyCallback([&](const bool _needMoreData)
		{
			onUCRxEmpty(_needMoreData);
		});

		m_hdiUC.setWriteTxCallback([this](const uint32_t _word)
		{
			if(m_boot.hdiWriteTX(_word))
				onDspBootFinished();
		});
		m_hdiUC.setWriteIrqCallback([&](const uint8_t _irq)
		{
			hdiSendIrqToDSP(_irq);
		});
		m_hdiUC.setReadIsrCallback([&](const uint8_t _isr)
		{
			return hdiUcReadIsr(_isr);
		});
		m_hdiUC.setInitHdi08Callback([&]
		{
			// clear init flag again immediately, code is waiting for it to happen
			m_hdiUC.icr(m_hdiUC.icr() & 0x7f);
			m_hdiUC.isr(m_hdiUC.isr() | mc68k::Hdi08::IsrBits::Txde | mc68k::Hdi08::IsrBits::Trdy);
		});

		m_irqInterruptDone = dsp().registerInterruptFunc([this]
		{
			m_triggerInterruptDone.notify();
		});
	}

	void DSP::terminate()
	{
		for(uint32_t i=0; i<32768; ++i)
			m_triggerInterruptDone.notify();

		m_thread->terminate();
	}

	void DSP::join() const
	{
		m_thread->join();
	}

	void DSP::onDspBootFinished()
	{
		m_hdiUC.setWriteTxCallback([&](const uint32_t _word)
		{
			hdiTransferUCtoDSP(_word);
		});

#if DSP56300_DEBUGGER
		if(!m_index)
			m_thread.reset(new dsp56k::DSPThread(dsp(), m_name.c_str(), std::make_shared<dsp56kDebugger::Debugger>(m_dsp)));
		else
#endif
		m_thread.reset(new dsp56k::DSPThread(dsp(), m_name.c_str()));

		m_thread->setLogToStdout(false);
	}

	void DSP::onUCRxEmpty(const bool _needMoreData)
	{
		if(_needMoreData)
		{
			hwLib::ScopedResumeDSP rA(m_hardware.getDSPA().getHaltDSP());
			hwLib::ScopedResumeDSP rB(m_hardware.getDSPB().getHaltDSP());

			while(dsp().hasPendingInterrupts())
				std::this_thread::yield();
		}
		hdiTransferDSPtoUC();
	}

	void DSP::hdiTransferUCtoDSP(const uint32_t _word)
	{
//		LOG('[' << m_name << "] toDSP writeRX=" << HEX(_word) << ", ucPC=" << HEX(m_hardware.getUC().getPrevPC()));
		hdi08().writeRX(&_word, 1);
	}

	void DSP::hdiSendIrqToDSP(const uint8_t _irq)
	{
		if(m_hardware.requestingHaltDSPs() && getHaltDSP().isHalting())
		{
			// this is a very hacky way to execute a DSP interrupt even though the DSP is halted. This case happens if the DSPs run too fast
			// and are halted by the sync code, but the UC wants to inject an interrupt, which needs to be executed immediately.
			// In this case, we execute the interrupt without altering the DSP state

			const auto numOps = dsp().getInstructionCounter();
			const auto numCycles = dsp().getCycles();

			const auto pc = dsp().getPC();
			dsp().getJit().exec(_irq);
			dsp().setPC(pc);

			const_cast<uint64_t&>(dsp().getInstructionCounter()) = numOps;
			const_cast<uint64_t&>(dsp().getCycles()) = numCycles;
		}
		else
		{
			dsp().injectExternalInterrupt(_irq);
			dsp().injectExternalInterrupt(m_irqInterruptDone);

			hwLib::ScopedResumeDSP rA(m_hardware.getDSPA().getHaltDSP());
			hwLib::ScopedResumeDSP rB(m_hardware.getDSPB().getHaltDSP());
			m_triggerInterruptDone.wait();
		}

		hdiTransferDSPtoUC();
	}

	uint8_t DSP::hdiUcReadIsr(uint8_t _isr)
	{
		hdiTransferDSPtoUC();

		// transfer DSP host flags HF2&3 to uc
		const auto hf23 = hdi08().readControlRegister() & 0x18;
		_isr &= ~0x18;
		_isr |= hf23;
		// always ready to receive more data
		_isr |= mc68k::Hdi08::IsrBits::Trdy;
		return _isr;
	}

	bool DSP::hdiTransferDSPtoUC()
	{
		if (m_hdiUC.canReceiveData() && hdi08().hasTX())
		{
			const auto v = hdi08().readTX();
//			LOG('[' << m_name << "] HDI dsp2UC=" << HEX(v));
			m_hdiUC.writeRx(v);
			return true;
		}
		return false;
	}
}
