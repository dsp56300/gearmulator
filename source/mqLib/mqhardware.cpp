#include "mqhardware.h"

#include "dsp56kEmu/interrupts.h"

#if EMBED_ROM
#include "romData.h"
#else
constexpr uint8_t* ROM_DATA = nullptr;
#endif

namespace mqLib
{
#if EMBED_ROM
	static_assert(ROM_DATA_SIZE == ROM::g_romSize);
#endif

	constexpr uint32_t g_syncEsaiFrameRate = 16;
	constexpr uint32_t g_syncHaltDspEsaiThreshold = 32;

	static_assert((g_syncEsaiFrameRate & (g_syncEsaiFrameRate - 1)) == 0, "esai frame sync rate must be power of two");
	static_assert(g_syncHaltDspEsaiThreshold >= g_syncEsaiFrameRate * 2, "esai DSP halt threshold must be greater than two times the sync rate");

	Hardware::Hardware(std::string _romFilename)
		: m_romFileName(std::move(_romFilename))
		, m_rom(m_romFileName, ROM_DATA)
		, m_uc(m_rom)
		, m_dspThread(m_dsp.dsp())
		, m_hdiUC(m_uc.hdi08())
		, m_hdiDSP(m_dsp.hdi08())
	{
		if(!m_rom.isValid())
			return;

		m_dspThread.setLogToStdout(false);
		m_dsp.getPeriph().disableTimers(true);	// only used to test DSP load, we report 0 all the time for now

		m_hdiDSP.setRXRateLimit(0);
		m_hdiDSP.setTransmitDataAlwaysEmpty(false);

		auto& esai = m_dsp.getPeriph().getEsai();

		esai.setCallback([&](dsp56k::Audio*)
		{
			++m_esaiFrameIndex;

			if((m_esaiFrameIndex & (g_syncEsaiFrameRate-1)) == 0)
				m_esaiFrameAddedCv.notify_one();

			m_requestedFramesAvailableMutex.lock();

			if(m_requestedFrames && esai.getAudioOutputs().size() >= m_requestedFrames)
			{
				m_requestedFramesAvailableMutex.unlock();
				m_requestedFramesAvailableCv.notify_one();
			}
			else
			{
				m_requestedFramesAvailableMutex.unlock();
			}

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

		for (const auto data : midiData)
			_data.push_back(data & 0xff);
	}

	void Hardware::setBootMode(const BootMode _mode)
	{
		auto setButton = [&](const Buttons::ButtonType _type, const bool _pressed = true)
		{
			m_uc.getButtons().setButton(_type, _pressed);
		};

		switch (_mode)
		{
		case BootMode::Default: 
			setButton(Buttons::ButtonType::Inst1, false);
			setButton(Buttons::ButtonType::Inst3, false);
			setButton(Buttons::ButtonType::Shift, false);
			setButton(Buttons::ButtonType::Global, false);
			setButton(Buttons::ButtonType::Multi, false);
			setButton(Buttons::ButtonType::Play, false);
			break;
		case BootMode::FactoryTest:
			setButton(Buttons::ButtonType::Inst1);
			setButton(Buttons::ButtonType::Global);
			break;
		case BootMode::EraseFlash:
			setButton(Buttons::ButtonType::Inst3);
			setButton(Buttons::ButtonType::Global);
			break;
		case BootMode::WaitForSystemDump:
			setButton(Buttons::ButtonType::Shift);
			setButton(Buttons::ButtonType::Global);
			break;
		case BootMode::DspClockResetAndServiceMode:
			setButton(Buttons::ButtonType::Multi);
			break;
		case BootMode::ServiceMode:
			setButton(Buttons::ButtonType::Global);
			break;
		case BootMode::MemoryGame:
			setButton(Buttons::ButtonType::Global);
			setButton(Buttons::ButtonType::Play);
			break;
		}
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
		if(m_hdiUC.canReceiveData() && m_hdiDSP.hasTX())
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
				return m_hdiDSP.txInterruptEnabled() && !m_hdiDSP.hasTX();
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

		hdiTransferDSPtoUC();

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

		{
			std::lock_guard uLockHalt(m_haltDSPmutex);
			m_haltDSP = false;
		}
		m_haltDSPcv.notify_one();
	}

	void Hardware::setGlobalDefaultParameters()
	{
		sendMidi({0xf0,0x3e,0x10,0x7f,0x24,0x00,0x07,0x02,0xf7});	// Control Send = SysEx
		sendMidi({0xf0,0x3e,0x10,0x7f,0x24,0x00,0x08,0x01,0xf7});	// Control Receive = on
		m_bootCompleted = true;
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

		if(esaiDelta > g_syncHaltDspEsaiThreshold)
		{
			haltDSP();
		}
		else
		{
			resumeDSP();
		}

		m_lastEsaiFrameIndex = esaiFrameIndex;
	}

	void Hardware::processAudio(uint32_t _frames, uint32_t _latency)
	{
		ensureBufferSize(_frames);

		if(m_esaiFrameIndex == 0)
			return;

		m_processAudio = true;

		auto& esai = m_dsp.getPeriph().getEsai();

		const dsp56k::TWord* inputs[16]{nullptr};
		dsp56k::TWord* outputs[16]{nullptr};

		inputs[0] = &m_audioInputs[0].front();
		inputs[1] = &m_audioInputs[1].front();

		outputs[0] = &m_audioOutputs[0].front();
		outputs[1] = &m_audioOutputs[1].front();
		outputs[2] = &m_audioOutputs[2].front();
		outputs[3] = &m_audioOutputs[3].front();
		outputs[4] = &m_audioOutputs[4].front();
		outputs[5] = &m_audioOutputs[5].front();

		while (_frames)
		{
			const auto processCount = std::min(_frames, static_cast<uint32_t>(1024));
			_frames -= processCount;

			esai.processAudioInputInterleaved(inputs, processCount, _latency);

			const auto requiredSize = processCount > 4 ? (processCount << 1) - 8 : 0;

			if(esai.getAudioOutputs().size() < requiredSize)
			{
				// reduce thread contention by waiting for output buffer to be full enough to let us grab the data without entering the read mutex too often

				std::unique_lock uLock(m_requestedFramesAvailableMutex);
				m_requestedFrames = requiredSize;
				m_requestedFramesAvailableCv.wait(uLock, [&]()
				{
					if(esai.getAudioOutputs().size() < requiredSize)
						return false;
					m_requestedFrames = 0;
					return true;
				});
			}

			esai.processAudioOutputInterleaved(outputs, processCount);

			inputs[0] += processCount;
			inputs[1] += processCount;

			outputs[0] += processCount;
			outputs[1] += processCount;
			outputs[2] += processCount;
			outputs[3] += processCount;
			outputs[4] += processCount;
			outputs[5] += processCount;
		}

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
