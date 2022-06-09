#include "mqhardware.h"

#include "dsp56kEmu/interrupts.h"

namespace mqLib
{
	Hardware::Hardware(std::string _romFilename)
		: m_romFileName(std::move(_romFilename))
		, m_rom(_romFilename)
		, m_uc(m_rom)
		, m_hdiUC(m_uc.hdi08())
		, m_hdiDSP(m_dsp.hdi08())
		, m_buttons(m_uc.getButtons())
		, m_dspThread(m_dsp.dsp())
	{
		m_hdiDSP.setRXRateLimit(0);
		m_hdiDSP.setTransmitDataAlwaysEmpty(false);

		m_dsp.getPeriph().getEsai().setCallback([&](dsp56k::Audio*)
		{
			++m_esaiFrameIndex;
		}, 0);

		m_dsp.dsp().setExecCallback([&]()
		{
			dspExecCallback();
		});
	}

	Hardware::~Hardware()
	{
		m_dsp.getPeriph().getEsai().setCallback({}, 0);
	}

	void Hardware::process(uint32_t _frames)
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
	}

	void Hardware::transferHostFlags()
	{
		// transfer HF 2&3 from uc to DSP
		auto hsr = m_hdiDSP.readStatusRegister();
		const auto prevHsr = hsr;
		hsr &= ~0x18;
		hsr |= (m_hdiHF01<<3);
		if(prevHsr != hsr)
			m_hdiDSP.writeStatusRegister(hsr);

		const auto hf23 = (m_hdiDSP.readControlRegister() >> 3) & 3;
		if(hf23 != m_hdiHF23)
		{
//			LOG("HDI HF23=" << HEXN(hf23,1));
			m_hdiHF23 = hf23;
		}
	}

	void Hardware::dspExecCallback()
	{
		transferHostFlags();
		if(m_hdiDSP.hasRXData())
			m_hdiDSP.exec();
	}

	void Hardware::injectUCtoDSPInterrupts()
	{
		if(m_requestNMI && !m_uc.requestDSPinjectNMI())
		{
			LOG("uc request DSP NMI");
			m_dsp.dsp().injectInterrupt(dsp56k::Vba_NMI);
		}

		m_requestNMI = m_uc.requestDSPinjectNMI();

		uint8_t interruptAddr;
		bool injected = false;
		while(m_hdiUC.pollInterruptRequest(interruptAddr))
		{
//			LOG("Inject interrupt " << HEXN(interruptAddr, 2));
			injected = true;
			if(!m_dsp.dsp().injectInterrupt(interruptAddr))
				LOG("Interrupt request FAILED, interrupt was masked");
		}

		while(m_dsp.dsp().hasPendingInterrupts())
		{
			ucYield();
		}
//		if(injected)
//			LOG("No interrupts pending");
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
		m_hdiUC.pollTx(txData);

		if(txData.empty())
			return;

		for (const uint32_t data : txData)
		{
			m_haveSentTXtoDSP = true;
//			LOG("toDSP writeRX=" << HEX(data));
			m_hdiDSP.writeRX(&data, 1);
		}
		txData.clear();
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
			int foo=0;
		}
	}

	void Hardware::processUcCycle()
	{
		const auto instructionCounter = m_dsp.dsp().getInstructionCounter();
		const auto d = dsp56k::delta(instructionCounter, m_dspInstructionCounter);
		m_dspInstructionCounter = instructionCounter;
		m_dspCycles += d;

		// we can only use ESAI once it has been enabled
		if(m_esaiFrameIndex > m_lastEsaiFrameIndex)
		{
			const auto mcClock = m_uc.getSim().getSystemClockHz();
			const auto mcCyclesPerFrame = mcClock / (44100 * 2);	// stereo interleaved

			m_remainingMcCycles += static_cast<int32_t>(mcCyclesPerFrame * (m_esaiFrameIndex - m_lastEsaiFrameIndex));
			m_lastEsaiFrameIndex = m_esaiFrameIndex;
		}
		if(m_esaiFrameIndex > 0)
		{
			if(m_remainingMcCycles < 0)
			{
				processAudio();
				std::this_thread::yield();
				return;
			}
		}
		/*else if(mc->getCycles() > dspCycles/5)
		{
			processAudio();
			std::this_thread::yield();
			continue;
		}*/

		const auto deltaCycles = m_uc.exec();
		if(m_esaiFrameIndex > 0)
			m_remainingMcCycles -= static_cast<int32_t>(deltaCycles);

		const uint32_t hf01 = (m_hdiUC.icr() >> 3) & 3;

		if(hf01 != m_hdiHF01)
		{
//			LOG("HDI HF01=" << HEXN(hf01,1));
			m_hdiHF01 = hf01;
		}

		injectUCtoDSPInterrupts();

		// transfer DSP host flags HF2&3 to uc
		auto isr = m_hdiUC.isr();
		const auto prevIsr = isr;
		isr &= ~0x18;
		isr |= (m_hdiHF23<<3);
		if(isr != prevIsr)
			m_hdiUC.isr(isr);

		hdiTransferUCtoDSP();
		hdiTransferDSPtoUC();

		if(m_uc.requestDSPReset())
		{
			if(m_haveSentTXtoDSP)
				m_uc.dumpMemory("DSPreset");
			assert(!haveSentTXtoDSP && "DSP needs reset even though it got data already. Needs impl");
			m_uc.notifyDSPBooted();
		}

		processAudio();
	}

	void Hardware::processAudio()
	{
		auto& esai = m_dsp.getPeriph().getEsai();

		const auto count = static_cast<uint32_t>(esai.getAudioOutputs().size())>>1;

		if(count < m_requestedSampleFrames)
			return;

//		LOG("Drain ESAI");
		const dsp56k::TWord* dummyInputs[16]{nullptr};
		dsp56k::TWord* dummyOutputs[16]{nullptr};
		dummyOutputs[0] = &m_audioOutputs[0].front();
		dummyOutputs[1] = &m_audioOutputs[1].front();
		esai.processAudioInterleaved(dummyInputs, dummyOutputs, count);
	}
}
