#include "mqdsp.h"

#include "dspBootCode.h"
#include "mqhardware.h"

#if DSP56300_DEBUGGER
#include "dsp56kDebugger/debugger.h"
#endif

#include "../mc68k/hdi08.h"

namespace mqLib
{
	static dsp56k::DefaultMemoryValidator g_memoryValidator;
	static constexpr dsp56k::TWord g_magicEsaiPacket = 0x654300;

	MqDsp::MqDsp(Hardware& _hardware, mc68k::Hdi08& _hdiUC, const uint32_t _index)
	: m_hardware(_hardware), m_hdiUC(_hdiUC)
	, m_index(_index)
	, m_name({static_cast<char>('A' + _index)})
	, m_periphX(nullptr)
	, m_memory(g_memoryValidator, g_pMemSize, g_xyMemSize, g_bridgedAddr, m_memoryBuffer)
	, m_dsp(m_memory, &m_periphX, &m_periphNop)
	{
		m_periphX.getEsaiClock().setExternalClockFrequency(44100 * 768); // measured as being roughly 33,9MHz, this should be exact
		m_periphX.getEsaiClock().setSamplerate(44100); // verified

		auto config = m_dsp.getJit().getConfig();

		config.aguSupportBitreverse = true;
		config.linkJitBlocks = true;
		config.dynamicPeripheralAddressing = true;
		config.maxInstructionsPerBlock = 0;	// TODO: needs to be 1 if DSP factory tests are run, to be investigated

		m_dsp.getJit().setConfig(config);

		// fill P memory with something that reminds us if we jump to garbage
		for(dsp56k::TWord i=0; i<m_memory.sizeP(); ++i)
			m_memory.set(dsp56k::MemArea_P, i, 0x000200);	// debug instruction

		// rewrite bootloader to work at address g_bootCodeBase instead of $ff0000
		for(uint32_t i=0; i<std::size(g_dspBootCode); ++i)
		{
			uint32_t code = g_dspBootCode[i];
			if((g_dspBootCode[i] & 0xffff00) == 0xff0000)
			{
				code = g_bootCodeBase | (g_dspBootCode[i] & 0xff);
			}

			m_memory.set(dsp56k::MemArea_P, i + g_bootCodeBase, code);
		}

//		m_memory.saveAssembly("dspBootDisasm.asm", g_bootCodeBase, static_cast<uint32_t>(std::size(g_dspBootCode)), true, true, &m_periphX, nullptr);

		// set OMR pins so that bootcode wants program data via HDI08 RX
		m_dsp.setPC(g_bootCodeBase);
		m_dsp.regs().omr.var |= OMR_MA | OMR_MB | OMR_MC | OMR_MD;

		getPeriph().disableTimers(true);	// only used to test DSP load, we report 0 all the time for now

		m_periphX.getEsai().writeEmptyAudioIn(8);

		hdi08().setRXRateLimit(0);
		hdi08().setTransmitDataAlwaysEmpty(false);

		m_hdiUC.setRxEmptyCallback([&](const bool needMoreData)
		{
			onUCRxEmpty(needMoreData);
		});
		m_hdiUC.setWriteTxCallback([&](const uint32_t _word)
		{
			hdiTransferUCtoDSP(_word);
		});
		m_hdiUC.setWriteIrqCallback([&](const uint8_t _irq)
		{
			hdiSendIrqToDSP(_irq);
		});
		m_hdiUC.setReadIsrCallback([&](const uint8_t _isr)
		{
			return hdiUcReadIsr(_isr);
		});

#if DSP56300_DEBUGGER
		m_thread.reset(new dsp56k::DSPThread(dsp(), m_name.c_str(), std::make_shared<dsp56kDebugger::Debugger>(m_dsp.dsp())));
#else
		m_thread.reset(new dsp56k::DSPThread(dsp(), m_name.c_str()));
#endif

		m_thread->setLogToStdout(false);
	}

	void MqDsp::exec()
	{
		m_thread->join();
		m_thread.reset();

		m_hdiUC.setRxEmptyCallback({});
		m_dsp.exec();
	}

	void MqDsp::dumpPMem(const std::string& _filename)
	{
		m_memory.saveAssembly((_filename + ".asm").c_str(), 0, g_pMemSize, true, false, &m_periphX);
	}

	void MqDsp::dumpXYMem(const std::string& _filename) const
	{
		m_memory.save((_filename + "_X.txt").c_str(), dsp56k::MemArea_X);
		m_memory.save((_filename + "_Y.txt").c_str(), dsp56k::MemArea_Y);
	}

	void MqDsp::transferHostFlagsUc2Dsdp()
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

	void MqDsp::onUCRxEmpty(bool _needMoreData)
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

	bool MqDsp::hdiTransferDSPtoUC()
	{
		if (m_hdiUC.canReceiveData() && hdi08().hasTX())
		{
			const auto v = hdi08().readTX();
//			LOG('[' << m_name << "] HDI dsp2uc=" << HEX(v));
			if (v == g_magicEsaiPacket)
				m_receivedMagicEsaiPacket = true;
			m_hdiUC.writeRx(v);
			return true;
		}
		return false;
	}

	void MqDsp::hdiTransferUCtoDSP(dsp56k::TWord _word)
	{
		m_haveSentTXtoDSP = true;
//		LOG('[' << m_name << "] toDSP writeRX=" << HEX(_word));
		hdi08().writeRX(&_word, 1);
	}

	void MqDsp::hdiSendIrqToDSP(uint8_t _irq)
	{
		waitDspRxEmpty();

		if(_irq == 0x92)
		{
//			LOG('[' << m_name << "] DSP timeout, waiting...");
			// this one is sent by the uc if the DSP taking too long to reset HF2 back to one. Instead of aborting here, we wait a bit longer for the DSP to finish on its own
			m_hardware.ucYieldLoop([&]
			{
				return !bittest(hdi08().readControlRegister(), dsp56k::HDI08::HCR_HF2);
			});
//			LOG('[' << m_name << "] DSP timeout wait done");
		}
//		else
//			LOG('[' << m_name << "] Inject interrupt" << HEXN(_irq,2));

		dsp().injectInterrupt(_irq);

		m_hardware.ucYieldLoop([&]()
		{
			return dsp().hasPendingInterrupts();
		});

		hdiTransferDSPtoUC();
	}

	uint8_t MqDsp::hdiUcReadIsr(uint8_t _isr)
	{
		// transfer DSP host flags HF2&3 to uc
		const auto hf23 = hdi08().readControlRegister() & 0x18;
		_isr &= ~0x18;
		_isr |= hf23;
		return _isr;
	}

	void MqDsp::waitDspRxEmpty()
	{
		m_hardware.ucYieldLoop([&]()
		{
			return (hdi08().hasRXData() && hdi08().rxInterruptEnabled()) || dsp().hasPendingInterrupts();
		});
//		LOG("writeRX wait over");
	}

}
