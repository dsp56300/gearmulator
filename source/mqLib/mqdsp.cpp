#include "mqdsp.h"

#include "mqhardware.h"

#if DSP56300_DEBUGGER
#include "dsp56kDebugger/debugger.h"
#endif

#include "mc68k/hdi08.h"

#include "dsp56kEmu/aar.h"

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
	, m_boot(m_dsp)
	{
		if(!_hardware.isValid())
			return;

		m_periphX.getEsaiClock().setExternalClockFrequency(44100 * 768); // measured as being roughly 33,9MHz, this should be exact

		m_periphX.getEsaiClock().setSamplerate(_hardware.useVoiceExpansion() ? 16 * 44100 : 44100);
		m_periphX.getEsaiClock().setClockSource(dsp56k::EsaiClock::ClockSource::Cycles);

		auto config = m_dsp.getJit().getConfig();

		config.aguSupportBitreverse = true;
		config.linkJitBlocks = true;
		config.dynamicPeripheralAddressing = false;
#ifdef _DEBUG
		config.debugDynamicPeripheralAddressing = true;
#endif
		config.maxInstructionsPerBlock = 0;	// TODO: needs to be 1 if DSP factory tests are run, to be investigated

		// allow dynamic peripheral addressing for code following clr b M_AAR3,r2
		enableDynamicPeripheralAddressing(config, m_dsp, 0x62f41b, dsp56k::M_AAR3, 16);

		m_dsp.getJit().setConfig(config);

		// fill P memory with something that reminds us if we jump to garbage
		for(dsp56k::TWord i=0; i<m_memory.sizeP(); ++i)
		{
			m_memory.set(dsp56k::MemArea_P, i, 0x000200);	// debug instruction
			m_dsp.getJit().notifyProgramMemWrite(i);
		}

		getPeriph().disableTimers(true);	// only used to test DSP load, we report 0 all the time for now

		m_periphX.getEsai().writeEmptyAudioIn(8);

		hdi08().setRXRateLimit(0);
		hdi08().setTransmitDataAlwaysEmpty(false);

		// Set MC68K-side callbacks for all DSPs.
		// On real hardware, all HDI08 interfaces are active from power-on.
		m_hdiUC.setRxEmptyCallback([&](const bool needMoreData)
		{
			onUCRxEmpty(needMoreData);
		});
		m_hdiUC.setWriteTxCallback([&](const uint32_t _word)
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
			waitDspRxEmpty();
			m_hdiHF01 = hf01;
			hdi08().setPendingHostFlags01(hf01);
		}
	}

	void MqDsp::onDspBootFinished()
	{
		m_haveSentTXtoDSP = true;

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

	void MqDsp::reset()
	{
		if (m_thread)
		{
			m_thread->terminate();
			m_thread->join();
			m_thread.reset();
		}

		// Reset DSP core and peripherals
		m_dsp.resetHW();

		// Clear program memory so DspBoot can reload firmware
		for(dsp56k::TWord i=0; i<m_memory.sizeP(); ++i)
		{
			m_memory.set(dsp56k::MemArea_P, i, 0x000200);
			m_dsp.getJit().notifyProgramMemWrite(i);
		}

		// Reset ESAI state
		m_periphX.getEsai().writeEmptyAudioIn(8);
		while (!m_periphX.getEsai().getAudioOutputs().empty())
			m_periphX.getEsai().getAudioOutputs().pop_front();

		// Reset boot state by reconstructing DspBoot in-place
		m_boot.~DspBoot();
		new (&m_boot) dsp56k::DspBoot(m_dsp);
		m_haveSentTXtoDSP = false;
		m_receivedMagicEsaiPacket = false;
		m_hdiHF01 = 0;

		// Restore initial HDI08 write callback for boot mode
		m_hdiUC.setWriteTxCallback([&](const uint32_t _word)
		{
			if(m_boot.hdiWriteTX(_word))
				onDspBootFinished();
		});
	}

	void MqDsp::onUCRxEmpty(bool _needMoreData)
	{
		if (!m_thread)
			return;

		hdi08().injectTXInterrupt();

		if (_needMoreData && m_hardware.getEsaiFrameIndex() > 0)
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
			auto v = hdi08().readTX();
			++m_hdiDspToUcCount;

			// The DSP firmware reads Port C bit 4 AFTER sending its first HDI08
			// response. On real VE hardware, the first response from expansion
			// DSPs would be 0x000010 (VE identity) because Port C is physically
			// tied high. In our emulation the timing differs, so we patch the
			// first response for expansion DSPs to match real hardware behavior.
			if (m_index > 0 && m_hdiDspToUcCount == 1 && v == 0x000001)
				v = 0x000010;

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
		hdi08().writeRX(&_word, 1);
	}

	void MqDsp::hdiSendIrqToDSP(uint8_t _irq)
	{
		if (!m_thread)
			return;

		waitDspRxEmpty();

		if(_irq == 0x92 && m_hardware.getEsaiFrameIndex() > 0)
		{
			m_hardware.ucYieldLoop([&]
			{
				return !bittest(hdi08().readControlRegister(), dsp56k::HDI08::HCR_HF2);
			});
		}

		dsp().injectExternalInterrupt(_irq);

		if (m_hardware.getEsaiFrameIndex() > 0)
		{
			m_hardware.ucYieldLoop([&]()
			{
				return dsp().hasPendingInterrupts();
			});
		}

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
	}

}
