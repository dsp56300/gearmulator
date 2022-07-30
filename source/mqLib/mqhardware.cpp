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
			m_ucWakeupCv.notify_one();
		}, 0);

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
	}

	Hardware::~Hardware()
	{
		m_dsp.getPeriph().getEsai().setCallback({}, 0);
		m_hdiUC.setRxEmptyCallback({});
	}

	bool Hardware::process()
	{
		processUcCycle();
		return true;
	}

	void Hardware::sendMidi(const uint8_t _byte)
	{
		m_uc.getQSM().writeSciRX(_byte);
	}

	void Hardware::receiveMidi(std::vector<uint8_t>& _data)
	{
		std::deque<uint16_t> midiData;
		m_uc.getQSM().readSciTX(midiData);
		if(midiData.empty())
			return;

		_data.clear();
		_data.reserve(midiData.size());

		for (auto data : midiData)
			_data.push_back(data & 0xff);
	}

	void Hardware::hdiProcessUCtoDSPNMIIrq()
	{
		// QS6 is connected to DSP NMI pin but I've never seen this being triggered
		const uint8_t requestNMI = m_uc.requestDSPinjectNMI();

		if(m_requestNMI && !requestNMI)
		{
			LOG("uc request DSP NMI");
			hdiSendIrqToDSP(dsp56k::Vba_NMI);
		}

		m_requestNMI = requestNMI;
	}

	void Hardware::hdiSendIrqToDSP(uint8_t _irq)
	{
		waitDspRxEmpty();

		while(!m_dsp.dsp().injectInterrupt(_irq))
			ucYield();

		while(m_dsp.dsp().hasPendingInterrupts())
			ucYield();
	}

	void Hardware::ucYield()
	{
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

	void Hardware::hdiTransferUCtoDSP(dsp56k::TWord _word)
	{
		m_haveSentTXtoDSP = true;
//		LOG("toDSP writeRX=" << HEX(_word));
		m_hdiDSP.writeRX(&_word, 1);
	}

	void Hardware::waitDspRxEmpty()
	{
		while((m_hdiDSP.hasRXData() && m_hdiDSP.rxInterruptEnabled()) || m_dsp.dsp().hasPendingInterrupts())
			ucYield();
//		LOG("writeRX wait over");
	}

	void Hardware::onUCRxEmpty(bool needMoreData)
	{
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
		if(m_remainingUcCycles <= 0)
		{
			// we can only use ESAI to clock the uc once it has been enabled
			if(m_esaiFrameIndex > 0)
			{
				if(m_esaiFrameIndex == m_lastEsaiFrameIndex)
				{
					std::unique_lock uLock(m_ucWakeupMutex);
					m_ucWakeupCv.wait(uLock);
				}

				const auto ucClock = m_uc.getSim().getSystemClockHz();
				const auto ucCyclesPerFrame = ucClock / (44100 * 2);	// stereo interleaved

				m_remainingUcCycles += static_cast<int32_t>(ucCyclesPerFrame * (m_esaiFrameIndex - m_lastEsaiFrameIndex));
				m_lastEsaiFrameIndex = m_esaiFrameIndex;
			}
		}

		// If ESAI is not enabled, we may roughly clock the uc to execute one op for each 5 DSP ops.
		/*
		if(m_esaiFrameIndex == 0 && m_uc.getCycles() > m_dspCycles/5)
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
			waitDspRxEmpty();
			m_hdiHF01 = hf01;
			m_hdiDSP.setPendingHostFlags01(hf01);
		}

		hdiProcessUCtoDSPNMIIrq();

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

		hdiTransferDSPtoUC();

		if(m_uc.requestDSPReset())
		{
			if(m_haveSentTXtoDSP)
				m_uc.dumpMemory("DSPreset");
			assert(!m_haveSentTXtoDSP && "DSP needs reset even though it got data already. Needs impl");
			m_uc.notifyDSPBooted();
		}
	}

	void Hardware::processAudio(uint32_t _frames)
	{
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

		auto& esai = m_dsp.getPeriph().getEsai();

		const auto count = _frames;

		const dsp56k::TWord* inputs[16]{nullptr};
		dsp56k::TWord* outputs[16]{nullptr};
		outputs[0] = &m_audioOutputs[0].front();
		outputs[1] = &m_audioOutputs[1].front();

		esai.processAudioInputInterleaved(inputs, count);

		// reduce thread contention by waiting for output buffer to be full enough to let us grab the data without entering the read mutex too often

		const auto requiredSize = (_frames << 1) - 8;

		while(esai.getAudioOutputs().size() < requiredSize)
			std::this_thread::yield();
//			std::this_thread::sleep_for(std::chrono::microseconds(20));

//		LOG("Out Size " << esai.getAudioOutputs().size() << ", in size " << esai.getAudioInputs().size());
		esai.processAudioOutputInterleaved(outputs, count);
	}
}
