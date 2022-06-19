#include "mqhardware.h"

#include "dsp56kEmu/interrupts.h"

namespace mqLib
{
	Hardware::Hardware(std::string _romFilename)
		: m_romFileName(std::move(_romFilename))
		, m_rom(m_romFileName)
		, m_uc(m_rom)
		, m_dspThread(m_dsp.dsp())
		, m_hdiUC(m_uc.hdi08())
		, m_hdiDSP(m_dsp.hdi08())
	{
		m_dspThread.setLogToStdout(false);
		m_dsp.getPeriph().disableTimers(true);	// only used to test DSP load, we report 0 all the time for now

		m_hdiDSP.setRXRateLimit(0);
		m_hdiDSP.setTransmitDataAlwaysEmpty(false);

		m_dsp.getPeriph().getEsai().setCallback([&](dsp56k::Audio*)
		{
			++m_esaiFrameIndex;
		}, 0);

		m_hdiUC.setRxEmptyCallback([&](bool needMoreData)
		{
			onUCRxEmpty(needMoreData);
		});
	}

	Hardware::~Hardware()
	{
		m_dsp.getPeriph().getEsai().setCallback({}, 0);
		m_hdiUC.setRxEmptyCallback({});
	}

	bool Hardware::process(uint32_t _frames)
	{
		m_requestedSampleFrames = _frames;

		if(m_audioInputs.front().size() < _frames)
		{
			for (auto& input : m_audioInputs)
				input.resize(_frames);
		}

		if(m_audioOutputs.front().size() < _frames)
		{
			for (auto& output : m_audioOutputs)
				output.resize(_frames);
		}

		if(m_esaiFrameIndex <= 0)
		{
			// execute the number of uc cycles that are roughly needed for N sample frames to complete

			const auto ucClock = m_uc.getSim().getSystemClockHz();
			const auto ucCycles = _frames * ucClock / (44100 * 2);	// stereo interleaved

			for(size_t i=0; i<ucCycles && m_esaiFrameIndex <= 0; ++i)
				processUcCycle();
			return false;
		}

		while(m_requestedSampleFrames)
			processUcCycle();
		/*
		const auto& esai = m_dsp.getPeriph().getEsai();

		while(m_remainingUcCycles > 0 && !esai.getAudioOutputs().full() && !esai.getAudioInputs().empty())
			processUcCycle();
		*/
		return true;
	}

	void Hardware::sendMidi(const uint8_t _byte)
	{
		m_uc.getQSM().writeSciRX(_byte);
	}

	void Hardware::injectUCtoDSPInterrupts()
	{
		bool injected = false;

		// QS6 is connected to DSP NMI pin but I've never seen this being triggered
		const uint8_t requestNMI = m_uc.requestDSPinjectNMI();

		if(m_requestNMI && !requestNMI)
		{
			LOG("uc request DSP NMI");
			injected = true;
			while(!m_dsp.dsp().injectInterrupt(dsp56k::Vba_NMI))
				ucYield();
		}

		m_requestNMI = requestNMI;

		uint8_t interruptAddr;
		while(m_hdiUC.pollInterruptRequest(interruptAddr))
		{
//			LOG("Inject interrupt " << HEXN(interruptAddr, 2));
			injected = true;
			while(!m_dsp.dsp().injectInterrupt(interruptAddr))
				ucYield();
		}

		if(!injected)
			return;

		while(m_dsp.dsp().hasPendingInterrupts())
			ucYield();

//		LOG("No interrupts pending");
	}

	void Hardware::ucYield()
	{
		processAudio();
		std::this_thread::yield();
	}

	bool Hardware::hdiTransferDSPtoUC()
	{
		if(m_hdiDSP.hasTX() && m_hdiUC.canReceiveData())
		{
			const auto v = m_hdiDSP.readTX();
//			LOG("HDI uc2dsp=" << HEX(v));
			m_hdiUC.writeRx(v);
			return true;
		}
		return false;
	}

	void Hardware::hdiTransferUCtoDSP()
	{
		m_hdiUC.pollTx(m_txData);

		if(m_txData.empty())
			return;

		for (const uint32_t data : m_txData)
		{
			m_haveSentTXtoDSP = true;
//			LOG("toDSP writeRX=" << HEX(data));
			m_hdiDSP.writeRX(&data, 1);
		}
		m_txData.clear();
		while((m_hdiDSP.hasRXData() && m_hdiDSP.rxInterruptEnabled()) || m_dsp.dsp().hasPendingInterrupts())
			ucYield();
//		LOG("writeRX wait over");
	}

	void Hardware::onUCRxEmpty(bool needMoreData)
	{
		injectUCtoDSPInterrupts();

		m_hdiDSP.injectTXInterrupt();

		if(needMoreData)
		{
			while(!m_hdiDSP.hasTX() && m_hdiDSP.txInterruptEnabled())
			{
				ucYield();
			}
		}

		if(!hdiTransferDSPtoUC())
		{
			int d=0;
		}
	}

	void Hardware::processUcCycle()
	{
		const auto instructionCounter = m_dsp.dsp().getInstructionCounter();
		const auto d = dsp56k::delta(instructionCounter, m_dspInstructionCounter);
		m_dspInstructionCounter = instructionCounter;
		m_dspCycles += d;

		// we can only use ESAI to clock the uc once it has been enabled
		if(m_esaiFrameIndex > m_lastEsaiFrameIndex)
		{
			const auto ucClock = m_uc.getSim().getSystemClockHz();
			const auto ucCyclesPerFrame = ucClock / (44100 * 2);	// stereo interleaved

			m_remainingUcCycles += static_cast<int32_t>(ucCyclesPerFrame * (m_esaiFrameIndex - m_lastEsaiFrameIndex));
			m_lastEsaiFrameIndex = m_esaiFrameIndex;
		}
		if(m_esaiFrameIndex > 0)
		{
			if(m_remainingUcCycles < 0)
			{
				ucYield();
				return;
			}
		}
		// If ESAI is not enabled, we roughly clock the uc to execute one op for each 5 DSP ops.
		/*
		else if(m_uc.getCycles() > m_dspCycles/5)
		{
			ucYield();
			return;
		}
		*/
		const auto deltaCycles = m_uc.exec();
		if(m_esaiFrameIndex > 0)
			m_remainingUcCycles -= static_cast<int32_t>(deltaCycles);

		const uint32_t hf01 = m_hdiUC.icr() & 0x18;

		if(hf01 != m_hdiHF01)
		{
//			LOG("HDI HF01=" << HEXN(hf01>>3,1));
			m_hdiHF01 = hf01;
			m_hdiDSP.setPendingHostFlags01(hf01);
		}

		injectUCtoDSPInterrupts();

		// transfer DSP host flags HF2&3 to uc
		const auto hf23 = m_hdiDSP.readControlRegister() & 0x18;
//		if(hf23 != m_hdiHF23)
		{
//			LOG("HDI HF23=" << HEXN(hf23>>3,1));
			m_hdiHF23 = hf23;
		}
		auto isr = m_hdiUC.isr();
		const auto prevIsr = isr;
		isr &= ~0x18;
		isr |= hf23;
		if(isr != prevIsr)
			m_hdiUC.isr(isr);

		hdiTransferUCtoDSP();
		hdiTransferDSPtoUC();

		if(m_uc.requestDSPReset())
		{
			if(m_haveSentTXtoDSP)
				m_uc.dumpMemory("DSPreset");
			assert(!m_haveSentTXtoDSP && "DSP needs reset even though it got data already. Needs impl");
			m_uc.notifyDSPBooted();
		}

		processAudio();
	}

	void Hardware::processAudio()
	{
		auto& esai = m_dsp.getPeriph().getEsai();

		const auto count = std::min(static_cast<uint32_t>(esai.getAudioOutputs().size()>>1), m_requestedSampleFrames);

		if(count < m_requestedSampleFrames)
			return;

//		LOG("Drain ESAI");
		const dsp56k::TWord* inputs[16]{nullptr};
		dsp56k::TWord* outputs[16]{nullptr};
		outputs[0] = &m_audioOutputs[0].front();
		outputs[1] = &m_audioOutputs[1].front();
		esai.processAudioInterleaved(inputs, outputs, count);
		m_requestedSampleFrames -= count;
	}
}
