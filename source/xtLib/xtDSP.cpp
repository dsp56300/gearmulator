#include "xtDSP.h"

#include "xtHardware.h"

#if DSP56300_DEBUGGER
#include "dsp56kDebugger/debugger.h"
#endif

#include "mc68k/hdi08.h"

#include "dsp56kEmu/aar.h"
#include "dsp56kEmu/types.h"

namespace xt
{
	static dsp56k::DefaultMemoryValidator g_memoryValidator;

	DSP::DSP(Hardware& _hardware, mc68k::Hdi08& _hdiUC, const uint32_t _index)
	: m_hardware(_hardware), m_hdiUC(_hdiUC)
	, m_index(_index)
	, m_name({static_cast<char>('A' + _index)})
	, m_periphX()
	, m_memory(g_memoryValidator, g_pMemSize, g_xyMemSize, g_bridgedAddr, m_memoryBuffer)
	, m_dsp(m_memory, &m_periphX, &m_periphNop)
	, m_boot(m_dsp)
	{
		if(!_hardware.isValid())
			return;

		m_periphX.getEssiClock().setExternalClockFrequency(10'240'000);	// 10,24 MHz
		m_periphX.getEssiClock().setSamplerate(40000);
		m_periphX.getEssiClock().setClockSource(dsp56k::EsaiClock::ClockSource::Cycles);

		auto config = m_dsp.getJit().getConfig();

		config.aguSupportBitreverse = true;
		config.linkJitBlocks = true;
		config.dynamicPeripheralAddressing = false;
#ifdef _DEBUG
		config.debugDynamicPeripheralAddressing = true;
#endif

		// allow dynamic peripheral addressing for code following clr b M_AAR3,r2
		enableDynamicPeripheralAddressing(config, m_dsp, 0x62f41b, dsp56k::M_AAR3, 16);

		m_dsp.getJit().setConfig(config);

		// fill P memory with something that reminds us if we jump to garbage
		for(dsp56k::TWord i=0; i<m_memory.sizeP(); ++i)
		{
			m_memory.set(dsp56k::MemArea_P, i, 0x000200);	// debug instruction
			m_dsp.getJit().notifyProgramMemWrite(i);
		}

//		getPeriph().disableTimers(true);

		m_periphX.getEssi0().writeEmptyAudioIn(8);

		hdi08().setRXRateLimit(0);
		hdi08().setTransmitDataAlwaysEmpty(false);

		m_hdiUC.setRxEmptyCallback([&](const bool needMoreData)
		{
			onUCRxEmpty(needMoreData);
		});
		m_hdiUC.setWriteTxCallback([&](const uint32_t _word)
		{
			if(m_boot.hdiWriteTX(_word))
				onDspBooted();
		});
		m_hdiUC.setWriteIrqCallback([&](const uint8_t _irq)
		{
			hdiSendIrqToDSP(_irq);
		});
		m_hdiUC.setReadIsrCallback([&](const uint8_t _isr)
		{
			return hdiUcReadIsr(_isr);
		});
	}

	void DSP::exec()
	{
		m_thread->join();
		m_thread.reset();

		m_hdiUC.setRxEmptyCallback({});
		m_dsp.exec();
	}

	void DSP::dumpPMem(const std::string& _filename)
	{
		m_memory.saveAssembly((_filename + ".asm").c_str(), 0, g_pMemSize, true, false, &m_periphX);
	}

	void DSP::dumpXYMem(const std::string& _filename) const
	{
		m_memory.save((_filename + "_X.txt").c_str(), dsp56k::MemArea_X);
		m_memory.save((_filename + "_Y.txt").c_str(), dsp56k::MemArea_Y);
	}

	void DSP::transferHostFlagsUc2Dsdp()
	{
		const uint32_t hf01 = m_hdiUC.icr() & 0x18;

		if (hf01 != m_hdiHF01)
		{
//			LOG('[' << m_name << "] HDI HF01=" << HEXN((hf01>>3),1));
			waitDspRxEmpty();
			m_hdiHF01 = hf01;
			hdi08().setPendingHostFlags01(hf01);
		}
	}

	void DSP::onDspBooted()
	{
		m_hdiUC.setWriteTxCallback([&](const uint32_t _word)
		{
			hdiTransferUCtoDSP(_word);
		});

#if DSP56300_DEBUGGER
		m_thread.reset(new dsp56k::DSPThread(dsp(), m_name.c_str(), std::make_shared<dsp56kDebugger::Debugger>(m_dsp)));
#else
		m_thread.reset(new dsp56k::DSPThread(dsp(), m_name.c_str()));
#endif

		m_thread->setLogToStdout(false);
	}

	void DSP::onUCRxEmpty(bool _needMoreData)
	{
		hdi08().injectTXInterrupt();

		if (_needMoreData)
		{
			m_hardware.ucYieldLoop([&]()
			{
				return dsp().hasPendingInterrupts() || (hdi08().txInterruptEnabled() && !hdi08().hasTX());
			});
		}

		hdiTransferDSPtoUC();
	}

	bool DSP::hdiTransferDSPtoUC()
	{
		if (m_hdiUC.canReceiveData() && hdi08().hasTX())
		{
			const auto v = hdi08().readTX();
//			LOG('[' << m_name << "] HDI dsp2uc=" << HEX(v));
			m_hdiUC.writeRx(v);
			return true;
		}
		return false;
	}

	void DSP::hdiTransferUCtoDSP(dsp56k::TWord _word)
	{
		m_haveSentTXtoDSP = true;
//		LOG('[' << m_name << "] toDSP writeRX=" << HEX(_word));

		// this can happen during "Reorganising Memory", DSP must resume to process incoming data
		if (hdi08().dataRXFull())
			m_hardware.resumeDSP();
		hdi08().writeRX(&_word, 1);
	}

	void DSP::hdiSendIrqToDSP(uint8_t _irq)
	{
		waitDspRxEmpty();

//		LOG('[' << m_name << "] Inject interrupt" << HEXN(_irq,2));

		dsp().injectExternalInterrupt(_irq);

		m_hardware.ucYieldLoop([&]()
		{
			return dsp().hasPendingInterrupts();
		});

		hdiTransferDSPtoUC();
	}

	uint8_t DSP::hdiUcReadIsr(uint8_t _isr)
	{
		// transfer DSP host flags HF2&3 to uc
		const auto hf23 = hdi08().readControlRegister() & 0x18;
		_isr &= ~0x18;
		_isr |= hf23;
		return _isr;
	}

	void DSP::waitDspRxEmpty()
	{
		m_hardware.ucYieldLoop([&]()
		{
			return (hdi08().hasRXData() && hdi08().rxInterruptEnabled()) || dsp().hasPendingInterrupts();
		});
//		LOG("writeRX wait over");
	}

}
