#include "mqdsp.h"

#include "mqhardware.h"

#include "baseLib/logging.h"

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

		// Enforce minimum DSP instruction gap between HRDF interrupts.
		// Real hardware: MC68K@16MHz writes TXH/TXM/TXL + polls TXDE ≈ 20-40
		// MC68K cycles per word ≈ 125-250 DSP@100MHz instruction cycles.
		hdi08().setRXRateLimit(100);
		hdi08().setTransmitDataAlwaysEmpty(false);

		// Set MC68K-side callbacks for all DSPs.
		// On real hardware, all HDI08 interfaces are active from power-on.
		// The writeTx callback is set once and never changed. A mutex protects
		// the boot/runtime mode switch and m_boot access against the boot pump
		// thread calling reset() concurrently.
		m_hdiUC.setRxEmptyCallback([&](const bool needMoreData)
		{
			onUCRxEmpty(needMoreData);
		});
		m_hdiUC.setWriteTxCallback([this](const uint32_t _word)
		{
			std::lock_guard lock(m_hdiCallbackMutex);
			if (m_inBootMode)
			{
				if(m_boot.hdiWriteTX(_word))
					onDspBootFinished();
			}
			else
			{
				hdiTransferUCtoDSP(_word);
			}
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
		// Safety: if a thread already exists, terminate it before creating a new one.
		// unique_ptr::reset(new ...) constructs the new object (starting its thread)
		// BEFORE destroying the old one, which would cause two threads on the same DSP.
		if (m_thread)
		{
			LOG("CRITICAL: DSP " << m_index << " onDspBootFinished called with existing thread! Terminating old thread first.");
			m_thread->terminate();
			m_thread->join();
			m_thread.reset();
		}

		m_haveSentTXtoDSP = true;
		m_hdiUcToDspCount = 0;

		LOG("DSP " << m_index << " boot finished, switching to runtime HDI08 callback");

		// Switch from boot to runtime mode. This is called from inside the
		// HDI08 callback which already holds m_hdiCallbackMutex.
		m_inBootMode = false;

#if DSP56300_DEBUGGER
		m_thread.reset(new dsp56k::DSPThread(dsp(), m_name.c_str(), std::make_shared<dsp56kDebugger::Debugger>(m_dsp)));
#else
		m_thread.reset(new dsp56k::DSPThread(dsp(), m_name.c_str()));
#endif

		m_thread->setLogToStdout(false);
	}

	void MqDsp::terminateThread()
	{
		if (m_thread)
		{
			m_thread->terminate();
			m_thread->join();
			m_thread.reset();
		}
	}

	void MqDsp::resetState()
	{
		// Reset DSP core and peripherals
		m_dsp.resetHW();

		// Resync ESAI clock after resetHW zeroed the instruction counter.
		// Without this, the EsaiClock's m_lastClock is far ahead of the
		// reset counter, causing broken timing until uint64 wraps around.
		m_periphX.getEsaiClock().restartClock();

		// Reset HDI08 transfer counter so VE identity patch fires correctly on next boot
		m_hdiDspToUcCount = 0;

		// Drain stale HDI08 TX data from previous boot cycle
		while (hdi08().hasTX())
			hdi08().readTX();

		// Clear UC-side HDI08 RX buffer so firmware doesn't read stale data
		m_hdiUC.clearRx();

		// Clear program memory so DspBoot can reload firmware
		for(dsp56k::TWord i=0; i<m_memory.sizeP(); ++i)
		{
			m_memory.set(dsp56k::MemArea_P, i, 0x000200);
			m_dsp.getJit().notifyProgramMemWrite(i);
		}

		// resetHW() clears Port C, but expansion DSPs need bit 4 (FST/GPIO)
		// set high to detect the VE clock signal. Re-apply after each reset.
		if (m_index > 0 && m_hardware.useVoiceExpansion())
		{
			m_periphX.getPortC().setControl(0x10);
			m_periphX.getPortC().hostWrite(0x10);
		}

		// Prime ESAI input with empty frames for boot pump routing.
		// resetHW() now properly resets ESAI (clearing buffers and registers),
		// so we just need to seed the input for the ESAI ring to start flowing.
		for (size_t i=0; i<8; ++i)
			m_periphX.getEsai().writeEmptyAudioIn(1);

		// Reset boot state by reconstructing DspBoot in-place.
		// Must hold the mutex to prevent the UC thread from calling
		// m_boot.hdiWriteTX() while we destroy and reconstruct it.
		{
			std::lock_guard lock(m_hdiCallbackMutex);
			m_boot.~DspBoot();
			new (&m_boot) dsp56k::DspBoot(m_dsp);
			m_inBootMode = true;
		}
		m_haveSentTXtoDSP = false;
		m_receivedMagicEsaiPacket = false;
		m_hdiHF01 = 0;
		m_hdiTransferFailCount = 0;
		m_hdiUcToDspCount = 0;
		m_hdiUcToDspLogIndex = 0;
	}

	void MqDsp::reset()
	{
		terminateThread();
		resetState();
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

		// Rate-limited diagnostic: log only sustained transfer failures
		if (hdi08().hasTX() && !m_hdiUC.canReceiveData())
		{
			if (++m_hdiTransferFailCount == 100000)
				LOG("DSP " << m_index << " HDI08 TX blocked: UC RX full for " << m_hdiTransferFailCount << " cycles");
		}
		else
		{
			m_hdiTransferFailCount = 0;
		}

		return false;
	}

	void MqDsp::hdiTransferUCtoDSP(dsp56k::TWord _word)
	{
		// TXDE back-pressure: wait for DSP to consume previous RX data.
		// On real hardware, the MC68K polls TXDE between words.
		if (hdi08().hasRXData() && hdi08().rxInterruptEnabled())
		{
			m_hardware.ucYieldLoop([&]()
			{
				return hdi08().hasRXData() && hdi08().rxInterruptEnabled();
			});
		}

		m_haveSentTXtoDSP = true;

		// Record in ring buffer for post-mortem analysis
		m_hdiUcToDspLog[m_hdiUcToDspLogIndex % g_hdiLogSize] = _word;
		++m_hdiUcToDspLogIndex;

		if (m_hdiUcToDspCount < 20)
			LOG("DSP " << m_index << " UC->DSP HDI08 word #" << m_hdiUcToDspCount << ": " << HEX(_word));
		++m_hdiUcToDspCount;

		hdi08().writeRX(&_word, 1);
	}

	void MqDsp::dumpHdiLog() const
	{
		const auto count = std::min(m_hdiUcToDspLogIndex, g_hdiLogSize);
		LOG("DSP " << m_index << " last " << count << " UC->DSP HDI08 words (total=" << m_hdiUcToDspCount << "):");
		for (uint32_t i = 0; i < count; ++i)
		{
			const auto idx = (m_hdiUcToDspLogIndex - count + i) % g_hdiLogSize;
			LOG("  [" << i << "] " << HEX(m_hdiUcToDspLog[idx]));
		}
	}

	void MqDsp::hdiSendIrqToDSP(uint8_t _irq)
	{
		if (!m_thread)
			return;

		const auto cmd = static_cast<HostCommand>(_irq);

		waitDspRxEmpty();

		// Before BatchStart, ensure the previous batch's command processing
		// is done.  Firmware protocol for Y:$6:
		//   BatchComplete: move n6,y:$6  => Y:$6 = N6 (bit 0 = 0 = pending)
		//   Main loop processes commands, then: bset #0,y:$6 => bit 0 = 1 = done
		// BatchStart clobbers R6 (fast interrupt, no save/restore).
		// If the command processor still has R6 at a non-standard offset,
		// subsequent HRDF writes go to wrong addresses.
		// On real hardware the MC68K is slow enough that processing always
		// finishes between batches.  Here we wait only before BatchStart
		// (not after BatchComplete) so the UC can do other work (LCD, MIDI,
		// other IRQs) in parallel with DSP command processing.
		if (cmd == HostCommand::BatchStart && m_commandProcessingActive)
		{
			m_hardware.ucYieldLoop([&]()
			{
				return (m_memory.get(dsp56k::MemArea_Y, 6) & 1) == 0;
			});
			m_commandProcessingActive = false;
		}

		if(cmd == HostCommand::SetHF2)
		{
			m_hardware.ucYieldLoop([&]
			{
				return !bittest(hdi08().readControlRegister(), dsp56k::HDI08::HCR_HF2);
			});
		}

		dsp().injectExternalInterrupt(_irq);

		// Wait for the external interrupt to be moved to the internal
		// pending queue by processExternalInterrupts().  Once there, it
		// sits ahead of any future HRDF in the FIFO, preserving ordering.
		// This is much cheaper than hasPendingInterrupts() which also
		// waits for long interrupt handlers to complete.
		m_hardware.ucYieldLoop([&]()
		{
			return dsp().hasPendingExternalInterrupts();
		});

		if (cmd == HostCommand::BatchComplete)
			m_commandProcessingActive = true;

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
			return (hdi08().hasRXData() && hdi08().rxInterruptEnabled()) || dsp().hasPendingExternalInterrupts();
		});
	}

}
