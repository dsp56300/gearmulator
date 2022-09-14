#include "mqhardware.h"

#include "dsp56kEmu/interrupts.h"

#include "romData.h"

namespace mqLib
{
	static_assert(ROM_DATA_SIZE == ROM::g_romSize);

	Hardware::Hardware(std::string _romFilename)
		: m_romFileName(std::move(_romFilename))
		, m_rom(m_romFileName, ROM_DATA)
		, m_uc(m_rom)
		, m_dspThread(m_dsp.dsp())
		, m_hdiUC(m_uc.hdi08())
		, m_hdiDSP(m_dsp.hdi08())
	{
		m_dspThread.setLogToStdout(false);
		m_dsp.getPeriph().disableTimers(true);	// only used to test DSP load, we report 0 all the time for now

		m_hdiDSP.setRXRateLimit(0);
		m_hdiDSP.setTransmitDataAlwaysEmpty(false);

		auto& esai = m_dsp.getPeriph().getEsai();

		esai.setCallback([&](dsp56k::Audio*)
		{
			++m_esaiFrameIndex;

			if((m_esaiFrameIndex & 15) == 0)
				m_esaiFrameAddedCv.notify_one();

			if(m_requestedFrames && esai.getAudioOutputs().size() >= m_requestedFrames)
				m_requestedFramesAvailableCv.notify_one();

			std::unique_lock uLock(m_haltDSPmutex);
			m_haltDSPcv.wait(uLock, [&]{ return m_haltDSP == false; });
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
		m_hdiUC.setReadIsrCallback([&](const uint8_t _isr)
		{
			return hdiUcReadIsr(_isr);
		});

		m_uc.getPortF().setDirectionChangeCallback([&](const mc68k::Port& port)
		{
			if(port.getDirection() == 0xff)
				setGlobalDefaultParameters();
		});
	}

	Hardware::~Hardware()
	{
		m_dsp.getPeriph().getEsai().setCallback({}, 0);
		m_hdiUC.setRxEmptyCallback({});
		m_dspThread.join();
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

	void Hardware::sendMidi(const std::vector<uint8_t>& _data)
	{
		for (const auto mb : _data)
			sendMidi(mb);
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
//			LOG("uc request DSP NMI");
			hdiSendIrqToDSP(dsp56k::Vba_NMI);

			m_requestNMI = requestNMI;
		}
	}

	void Hardware::hdiSendIrqToDSP(uint8_t _irq)
	{
		waitDspRxEmpty();

		m_dsp.dsp().injectInterrupt(_irq);

		ucYieldLoop([&]()
		{
			return m_dsp.dsp().hasPendingInterrupts();
		});
	}

	uint8_t Hardware::hdiUcReadIsr(uint8_t _isr) const
	{
		// transfer DSP host flags HF2&3 to uc
		const auto hf23 = m_hdiDSP.readControlRegister() & 0x18;
		_isr &= ~0x18;
		_isr |= hf23;
		return _isr;
	}

	void Hardware::ucYieldLoop(const std::function<bool()>& _continue)
	{
		const auto dspHalted = m_haltDSP;

		resumeDSP();

		while(_continue())
		{
			if(m_processAudio)
			{
				std::this_thread::yield();
			}
			else
			{
				std::unique_lock uLock(m_esaiFrameAddedMutex);
				m_esaiFrameAddedCv.wait(uLock);
			}
		}

		if(dspHalted)
			haltDSP();
	}

	bool Hardware::hdiTransferDSPtoUC() const
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
		ucYieldLoop([&]()
		{
			return (m_hdiDSP.hasRXData() && m_hdiDSP.rxInterruptEnabled()) || m_dsp.dsp().hasPendingInterrupts();
		});
//		LOG("writeRX wait over");
	}

	void Hardware::onUCRxEmpty(bool needMoreData)
	{
		m_hdiDSP.injectTXInterrupt();

		if(needMoreData)
		{
			ucYieldLoop([&]()
			{
				return !m_hdiDSP.hasTX() && m_hdiDSP.txInterruptEnabled();
			});
		}

		hdiTransferDSPtoUC();
	}

	void Hardware::processUcCycle()
	{
		syncUcToDSP();

		const auto deltaCycles = m_uc.exec();
		if(m_esaiFrameIndex > 0)
			m_remainingUcCycles -= static_cast<int64_t>(deltaCycles);

		const uint32_t hf01 = m_hdiUC.icr() & 0x18;

		if(hf01 != m_hdiHF01)
		{
//			LOG("HDI HF01=" << HEXN(hf01>>3,1));
			waitDspRxEmpty();
			m_hdiHF01 = hf01;
			m_hdiDSP.setPendingHostFlags01(hf01);
		}

		hdiProcessUCtoDSPNMIIrq();

		// it works not to feed the rx port, but only rely on the uc callback if its empty. We seem to be lucky though, uc might rely on the status of the rxdf bit
//		hdiTransferDSPtoUC();

		if(m_uc.requestDSPReset())
		{
			if(m_haveSentTXtoDSP)
				m_uc.dumpMemory("DSPreset");
			assert(!m_haveSentTXtoDSP && "DSP needs reset even though it got data already. Needs impl");
			m_uc.notifyDSPBooted();
		}
	}

	void Hardware::haltDSP()
	{
		if(m_haltDSP)
			return;

		std::lock_guard uLockHalt(m_haltDSPmutex);
		m_haltDSP = true;
	}

	void Hardware::resumeDSP()
	{
		if(!m_haltDSP)
			return;

		std::lock_guard uLockHalt(m_haltDSPmutex);
		m_haltDSP = false;
		m_haltDSPcv.notify_one();
	}

	void Hardware::setGlobalDefaultParameters()
	{
		sendMidi({0xf0,0x3e,0x10,0x00,0x24,0x00,0x07,0x02,0xf7});	// Control Send = SysEx
		sendMidi({0xf0,0x3e,0x10,0x00,0x24,0x00,0x08,0x01,0xf7});	// Control Receive = on
	}

	void Hardware::syncUcToDSP()
	{
		if(m_remainingUcCycles > 0)
			return;

		// we can only use ESAI to clock the uc once it has been enabled
		if(m_esaiFrameIndex <= 0)
			return;

		if(m_esaiFrameIndex == m_lastEsaiFrameIndex)
		{
			resumeDSP();
			std::unique_lock uLock(m_esaiFrameAddedMutex);
			m_esaiFrameAddedCv.wait(uLock, [this]{return m_esaiFrameIndex > m_lastEsaiFrameIndex;});
		}

		const auto esaiFrameIndex = m_esaiFrameIndex;

		const auto ucClock = m_uc.getSim().getSystemClockHz();

		constexpr double divInv = 1.0 / (44100.0 * 2.0);	// stereo interleaved
		const double ucCyclesPerFrame = static_cast<double>(ucClock) * divInv;

		const auto esaiDelta = esaiFrameIndex - m_lastEsaiFrameIndex;

		m_remainingUcCyclesD += ucCyclesPerFrame * static_cast<double>(esaiDelta);
		m_remainingUcCycles = static_cast<int64_t>(m_remainingUcCyclesD);
		m_remainingUcCyclesD -= static_cast<double>(m_remainingUcCycles);

		if((esaiFrameIndex - m_lastEsaiFrameIndex) > 64)
		{
			haltDSP();
		}
		else
		{
			resumeDSP();
		}

		m_lastEsaiFrameIndex = esaiFrameIndex;
	}

	void Hardware::processAudio(uint32_t _frames)
	{
		m_processAudio = true;

		ensureBufferSize(_frames);

		auto& esai = m_dsp.getPeriph().getEsai();

		const auto count = _frames;

		const dsp56k::TWord* inputs[16]{nullptr};
		dsp56k::TWord* outputs[16]{nullptr};
		outputs[0] = &m_audioOutputs[0].front();
		outputs[1] = &m_audioOutputs[1].front();

		esai.processAudioInputInterleaved(inputs, count);

		const auto requiredSize = _frames > 4 ? (_frames << 1) - 8 : 0;

		if(esai.getAudioOutputs().size() < requiredSize)
		{
			// reduce thread contention by waiting for output buffer to be full enough to let us grab the data without entering the read mutex too often

			m_requestedFrames = requiredSize;
			std::unique_lock uLock(m_requestedFramesAvailableMutex);
			m_requestedFramesAvailableCv.wait(uLock, [&]()
			{
				return esai.getAudioOutputs().size() >= requiredSize;
			});

			m_requestedFrames = 0;
		}

		esai.processAudioOutputInterleaved(outputs, count);

		m_processAudio = false;
	}

	void Hardware::ensureBufferSize(uint32_t _frames)
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
}
