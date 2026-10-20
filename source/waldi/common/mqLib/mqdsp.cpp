#include "mqdsp.h"

#include "mqhardware.h"

#include "baseLib/logging.h"

#include <thread>

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

		// No RX rate limiting — the emulated UC is fast enough that artificial
		// throttling causes audio thread stalls and dropouts at low latency.
		hdi08().setRXRateLimit(0);
		hdi08().setTransmitDataAlwaysEmpty(false);

		// Set MC68K-side callbacks for all DSPs.
		// On real hardware, all HDI08 interfaces are active from power-on.
		// All callbacks run on the UC thread — no cross-thread synchronization needed.
		m_hdiUC.setRxEmptyCallback([&](const bool needMoreData)
		{
			onUCRxEmpty(needMoreData);
		});
		setBootCallbacks();
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

	void MqDsp::setBootCallbacks()
	{
		m_hdiUC.setWriteTxCallback([this](const uint32_t _word)
		{
			if(m_boot.hdiWriteTX(_word))
				onDspBootFinished();
		});
	}

	void MqDsp::setRuntimeCallbacks()
	{
		m_hdiUC.setWriteTxCallback([this](const uint32_t _word)
		{
			hdiTransferUCtoDSP(_word);
		});
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

		LOG("DSP " << m_index << " boot finished, switching to runtime HDI08 callback");

		// Swap callback from boot to runtime. Called from inside the boot
		// callback itself (UC thread) — safe because the current invocation
		// is about to return and future calls will use the new callback.
		setRuntimeCallbacks();

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

		// a batch the old program had pending is gone with it, nothing waits for it, see waitBatchProcessed
		m_commandProcessingActive = false;

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

		// Swap to boot callbacks before reconstructing m_boot. Both this
		// function and the callback run on the UC thread, so no lock needed.
		setBootCallbacks();
		m_boot.~DspBoot();
		new (&m_boot) dsp56k::DspBoot(m_dsp);
		m_haveSentTXtoDSP = false;
		m_receivedMagicEsaiPacket = false;
		m_hdiHF01 = 0;
		m_hdiTransferFailCount = 0;
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

		if (_needMoreData)
		{
			if (m_hardware.getEsaiFrameIndex() > 0)
			{
				m_hardware.ucYieldLoop([&]()
				{
					return dsp().hasPendingInterrupts() || (hdi08().txInterruptEnabled() && !hdi08().hasTX());
				});
			}
			else if (m_index == 0 && m_hardware.useVoiceExpansion() && !m_receivedMagicEsaiPacket)
			{
				// During VE boot, the UC polls DSP A's HDI08 RX for the magic $654300.
				// We can't block/sleep the UC (DSPs need ongoing UC interaction at
				// real-time pace), but a scheduler yield gives DSP threads a chance
				// to run without meaningfully slowing the UC.
				std::this_thread::yield();
			}
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
#if defined(_WIN32) && defined(_DEBUG)
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
#endif
		return false;
	}

	void MqDsp::hdiTransferUCtoDSP(dsp56k::TWord _word)
	{
		waitBatchProcessed();

		m_haveSentTXtoDSP = true;

		hdi08().writeRX(&_word, 1);
	}

	void MqDsp::hdiSendIrqToDSP(uint8_t _irq)
	{
		if (!m_thread)
			return;

		const auto cmd = static_cast<HostCommand>(_irq);

		// the batch that BatchComplete handed over must be processed before anything else arrives, see there
		waitBatchProcessed();

		// $92 is the firmware's timeout/abort path: the UC sends it when it
		// thinks the DSP is too slow to set HF2=1.  In emulation the DSP is
		// fast enough that HF2=1 is already set by the time we get here, so
		// the IRQ would be a no-op.  Wait for HF2=1 as the batch barrier and
		// skip the injection + pending-interrupt wait entirely.
		if(cmd == HostCommand::SetHF2)
		{
			m_hardware.ucYieldLoop([&]
			{
				return !bittest(hdi08().readControlRegister(), dsp56k::HDI08::HCR_HF2);
			});
			hdiTransferDSPtoUC();
			return;
		}

		waitDspRxEmpty();

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

		// BatchComplete ends with `move n6,y:aa`, aa is the flag of its main loop, see waitBatchProcessed
		if (cmd == HostCommand::BatchComplete)
		{
			const auto op = m_memory.get(dsp56k::MemArea_P, static_cast<dsp56k::TWord>(HostCommand::BatchComplete) + 1);
			m_commandProcessingActive = (op & 0xffc0ff) == 0x7e0000;
			m_batchFlagAddr = (op >> 8) & 0x3f;
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
			return (hdi08().hasRXData() && hdi08().rxInterruptEnabled()) || dsp().hasPendingExternalInterrupts();
		});
	}

	/*	A DSP receives a batch of commands into a buffer at n6, the receive interrupt writes every word to (r6)+.
		BatchComplete ($82) points r6 back to the start of the buffer and marks the batch pending: `move n6,y:aa`
		clears bit 0 of a flag, Y:$6 on the microQ. The main loop parses the batch later, from its wait for the next
		audio block, and sets bit 0 again when it is done.

		Whatever the UC sends in between overwrites the head of the batch: the next BatchStart, a host command that
		resets r6, the data words of the next command. The DSP then parses a data word as a command, writes into the
		wrong records and jumps to a data value; the JIT dispatcher ran off the end of P memory and called a null entry,
		or the stack overflowed. That ended most playbacks with voice expansion under load.

		On hardware the DSP is so much faster than the UC that the main loop has long taken the batch. Here its thread
		may not even have run since. Everything that goes to the DSP after BatchComplete therefore waits until the batch
		is done; not BatchComplete itself, the UC goes on with the other DSPs meanwhile.

		BatchComplete has to be taken first, until then bit 0 still says done from the batch before: it waits among
		the pending interrupts, and while the DSP runs it the processing mode is FastInterrupt. Asked in this order,
		like hasPendingInterrupts.
	*/
	void MqDsp::waitBatchProcessed()
	{
		if(!m_commandProcessingActive)
			return;

		m_commandProcessingActive = false;

		constexpr auto batchComplete = static_cast<dsp56k::TWord>(HostCommand::BatchComplete);

		m_hardware.ucYieldLoop([&]()
		{
			return dsp().hasPendingInterrupt(batchComplete) || dsp().getProcessingMode() == dsp56k::DSP::FastInterrupt
				|| (m_memory.get(dsp56k::MemArea_Y, m_batchFlagAddr) & 1) == 0;
		});
	}

}
