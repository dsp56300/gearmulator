#include "mqhardware.h"

#include "synthLib/midiBufferParser.h"
#include "synthLib/deviceException.h"

#include <cstring>	// memcpy

namespace
{
	// Convert a TX frame to an RX frame for ESAI routing between DSPs.
	// TX slots have 6 words, RX slots have 4 — copy the first 4 words of each slot.
	void txToRx(const dsp56k::Audio::TxFrame& _tx, dsp56k::Audio::RxFrame& _rx)
	{
		_rx.resize(_tx.size());
		for (uint32_t s = 0; s < _tx.size(); ++s)
		{
			for (uint32_t w = 0; w < dsp56k::Audio::RxRegisterCount; ++w)
				_rx[s][w] = _tx[s][w];
		}
	}
}

namespace mqLib
{
	Hardware::Hardware(const ROM& _rom)
		: wLib::Hardware(44100)
		, m_rom(_rom)
		, m_uc(m_rom)
#if MQ_VOICE_EXPANSION
		, m_dsps{MqDsp(*this, m_uc.getHdi08A().getHdi08(), 0), MqDsp(*this, m_uc.getHdi08B().getHdi08(), 1), MqDsp(*this, m_uc.getHdi08C().getHdi08(), 2)}
#else
		, m_dsps{MqDsp(*this, m_uc.getHdi08A().getHdi08(), 0)}
#endif
		, m_midi(m_uc.getQSM(), 44100)
	{
		if(!m_rom.isValid())
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing);

#if MQ_VOICE_EXPANSION
		// On real VE hardware, Port C bit 4 (FST pin) is physically tied high on
		// the expansion board, telling DSP firmware this is a VE expansion DSP.
		// We must set both the external pin value (hostWrite) AND enable the pin
		// as GPIO input (setControl) before firmware boot. After reset, PCRC=0
		// means all pins are disconnected and dspRead returns 0 regardless of
		// hostWrite. Pre-enabling PCRC bit 4 ensures the firmware can read it
		// immediately during its early identification sequence.
		if (m_dsps.size() >= 3)
		{
			for (size_t i = 1; i < m_dsps.size(); ++i)
			{
				auto& portC = m_dsps[i].getPeriph().getPortC();
				portC.setControl(0x10);   // Enable bit 4 as GPIO (not ESAI)
				portC.hostWrite(0x10);    // Set external pin value: bit 4 = VE expansion
			}

			// Set ESAI clock dividers immediately so frame rates are aligned from
			// the very first frame, including during boot.  In real hardware all 3
			// DSPs share a single ESAI bit-clock so every frame period contains the
			// maximum slot count (32).  In the emulator each DSP has its own
			// EsxiClock, so we compensate with per-direction dividers:
			//   A: 2 TX slots → txDiv=15 (fire every 16th tick → 32 ticks/frame)
			//   B: 2 RX slots → rxDiv=15 (fire every 16th tick → 32 ticks/frame)
			//   C: 32 TX+RX  → no divider needed
			m_dsps[0].getPeriph().getEsaiClock().setEsaiDivider(&m_dsps[0].getPeriph().getEsai(), 15, 0);
			m_dsps[1].getPeriph().getEsaiClock().setEsaiDivider(&m_dsps[1].getPeriph().getEsai(), 0, 15);
			m_dsps[2].getPeriph().getEsaiClock().setEsaiDivider(&m_dsps[2].getPeriph().getEsai(), 0, 0);
		}
#endif

		m_uc.getPortF().setDirectionChangeCallback([&](const mc68k::Port& _port)
		{
			if(_port.getDirection() == 0xff)
				setGlobalDefaultParameters();
		});
	}

	Hardware::~Hardware()
	{
		for (auto & dsp : m_dsps)
			dsp.getPeriph().getEsai().setCallback({}, 0);
	}

	void Hardware::process()
	{
		processUcCycle();
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

	void Hardware::resetMidiCounter()
	{
		// wait for DSP to enter blocking state

		const auto& esai = m_dsps.front().getPeriph().getEsai();

		auto& inputs = esai.getAudioInputs();
		auto& outputs = esai.getAudioOutputs();

		while(inputs.size() > 2 && !outputs.full())
			std::this_thread::yield();

		m_midiOffsetCounter = 0;
	}

	void Hardware::hdiProcessUCtoDSPNMIIrq()
	{
		// QS6 is connected to DSP NMI pin but I've never seen this being triggered
#if SUPPORT_NMI_INTERRUPT
		const uint8_t requestNMI = m_uc.requestDSPinjectNMI();

		if(m_requestNMI && !requestNMI)
		{
//			LOG("uc request DSP NMI");
			m_dsps.front().hdiSendIrqToDSP(dsp56k::Vba_NMI);

			m_requestNMI = requestNMI;
		}
#endif
	}

	void Hardware::initVoiceExpansion()
	{
		if (m_dsps.size() < 3)
		{
			setupEsaiListener();
			return;
		}

		m_dsps[1].getPeriph().getPortC().hostWrite(0x10);	// set bit 4 of GPIO Port C, vexp DSPs are waiting for this
		m_dsps[2].getPeriph().getPortC().hostWrite(0x10);	// set bit 4 of GPIO Port C, vexp DSPs are waiting for this

		auto& esaiA = m_dsps[0].getPeriph().getEsai();
		auto& esaiB = m_dsps[1].getPeriph().getEsai();
		auto& esaiC = m_dsps[2].getPeriph().getEsai();

		// During boot, set empty callbacks on all ESAI interfaces to prevent DSP threads from blocking
		esaiA.setCallback([](dsp56k::Audio*) {}, 0);
		esaiB.setCallback([](dsp56k::Audio*) {}, 0);
		esaiC.setCallback([](dsp56k::Audio*) {}, 0);

		// Boot pump loop: route ESAI data in a ring between all 3 DSPs
		// Ring: A TX → B RX → B TX → C RX → C TX → A RX (matches XT topology)
		// DSP A sends 0x654300 via HDI08 TX when VE boot is ready
		uint32_t loopCount = 0;
		uint32_t routedAB = 0, routedBC = 0, routedCA = 0;

		while (true)
		{
			auto& txOutA = esaiA.getAudioOutputs();
			auto& txOutB = esaiB.getAudioOutputs();
			auto& txOutC = esaiC.getAudioOutputs();
			auto& rxInA  = esaiA.getAudioInputs();
			auto& rxInB  = esaiB.getAudioInputs();
			auto& rxInC  = esaiC.getAudioInputs();

			// A TX → B RX
			while (!txOutA.empty() && !rxInB.full())
			{
				dsp56k::Audio::RxFrame rx;
				txToRx(txOutA.pop_front(), rx);
				rxInB.push_back(std::move(rx));
				++routedAB;
			}

			// B TX → C RX
			while (!txOutB.empty() && !rxInC.full())
			{
				dsp56k::Audio::RxFrame rx;
				txToRx(txOutB.pop_front(), rx);
				rxInC.push_back(std::move(rx));
				++routedBC;
			}

			// C TX → A RX
			while (!txOutC.empty() && !rxInA.full())
			{
				dsp56k::Audio::RxFrame rx;
				txToRx(txOutC.pop_front(), rx);
				rxInA.push_back(std::move(rx));
				++routedCA;
			}

			++loopCount;

			// Exit when UC firmware signals boot complete AND all 3 DSPs have exchanged
			// ESAI data in the ring (C→A confirms full ring routing is working)
			if (m_bootCompleted && routedCA > 0)
				break;

			std::this_thread::yield();
		}

		LOG("Voice Expansion initialization completed");

		// Block further DSP resets now that VE init is complete
		m_voiceExpansionReady = true;

		// ESAI clock dividers were already set in the Hardware constructor
		// (before any DSP threads started) to ensure frame rates are aligned
		// from the very first frame.

		// Set up A's ESAI callback for frame counting + CV notification
		setupEsaiListener();

		// Drain any stale boot-time output and prime each DSP with fresh input
		for (auto& dsp : m_dsps)
		{
			auto& out = dsp.getPeriph().getEsai().getAudioOutputs();
			while (!out.empty())
				out.pop_front();
			dsp.getPeriph().getEsai().writeEmptyAudioIn(8);
		}

		// Normal ring routing: B→C, C→A via TX callbacks.
		// A→B handled in processAudio alongside host audio extraction.

		m_dsps[1].getPeriph().getEsai().setCallback([this](dsp56k::Audio* _audio)
		{
			auto& txOut = _audio->getAudioOutputs();
			auto& rxIn  = m_dsps[2].getPeriph().getEsai().getAudioInputs();
			while (!txOut.empty())
			{
/*				if (rxIn.full())
				{
					txOut.pop_front();
					continue;
				}
*/				dsp56k::Audio::RxFrame rx;
				txToRx(txOut.front(), rx);
				txOut.pop_front();
				rxIn.push_back(std::move(rx));
			}
		}, 0);

		m_dsps[2].getPeriph().getEsai().setCallback([this](dsp56k::Audio* _audio)
		{
			auto& txOut = _audio->getAudioOutputs();
			auto& rxIn  = m_dsps[0].getPeriph().getEsai().getAudioInputs();
			while (!txOut.empty())
			{
/*				if (rxIn.full())
				{
					txOut.pop_front();
					continue;
				}
*/				dsp56k::Audio::RxFrame rx;
				txToRx(txOut.front(), rx);
				txOut.pop_front();
				rxIn.push_back(std::move(rx));
			}
		}, 0);
		/*
		// Dump all DSP P memories as disassembly
		const char* dspNames[] = {"A", "B", "C"};
		for (uint32_t i = 0; i < m_dsps.size(); ++i)
		{
			const auto filename = std::string("mqDsp") + dspNames[i] + "_P.asm";
			const auto& mem = m_dsps[i].dsp().memory();
			mem.saveAssembly(filename.c_str(), 0, MqDsp::g_pMemSize, false, false);
			LOG("Saved DSP " << dspNames[i] << " P memory to " << filename);
		}
		*/
	}

	void Hardware::setupEsaiListener()
	{
		auto& esaiA = m_dsps.front().getPeriph().getEsai();

		esaiA.setCallback([&](dsp56k::Audio*)
		{
			onEsaiCallback(esaiA);
		}, 0);
	}

	void Hardware::processUcCycle()
	{
		syncUcToDSP();

		const auto deltaCycles = m_uc.exec();
		if(m_esaiFrameIndex > 0)
			m_remainingUcCycles -= static_cast<int64_t>(deltaCycles);

		for (size_t i = 0; i < m_dsps.size(); ++i)
			m_dsps[i].transferHostFlagsUc2Dsdp();

		hdiProcessUCtoDSPNMIIrq();

		for (auto& dsp : m_dsps)
			dsp.hdiTransferDSPtoUC();

		if(m_uc.requestDSPReset() && !m_voiceExpansionReady)
		{
			// Only actually reset if DSPs haven't booted yet
			bool allBooted = true;
			for (auto& dsp : m_dsps)
			{
				if(!dsp.haveSentTXToDSP())
				{
					allBooted = false;
					break;
				}
			}

			if(!allBooted && !m_dspResetPending)
			{
				for (auto& dsp : m_dsps)
					dsp.reset();
				m_dspResetPending = true;
			}
			m_uc.notifyDSPBooted();
		}

		// Clear pending when UC de-asserts reset (allows next reset cycle)
		if(m_dspResetPending && !m_uc.requestDSPReset())
			m_dspResetPending = false;
	}

	void Hardware::setGlobalDefaultParameters()
	{
		m_midi.write({0xf0,0x3e,0x10,0x7f,0x24,0x00,0x07,0x02,0xf7});	// Control Send = SysEx
		m_midi.write({0xf0,0x3e,0x10,0x7f,0x24,0x00,0x08,0x01,0xf7});	// Control Receive = on
		m_bootCompleted = true;
	}

	void Hardware::processAudio(uint32_t _frames, uint32_t _latency)
	{
		ensureBufferSize(_frames);

		if(m_esaiFrameIndex == 0)
			return;

		m_midi.process(_frames);

		m_processAudio = true;

		auto& esai = m_dsps.front().getPeriph().getEsai();

		const dsp56k::TWord* inputs[16]{nullptr};
		dsp56k::TWord* outputs[16]{nullptr};

		// TODO: Right audio input channel needs to be delayed by one frame

		inputs[0] = &m_audioInputs[0].front();
		inputs[1] = &m_audioInputs[1].front();
		inputs[2] = m_dummyInput.data();
		inputs[3] = m_dummyInput.data();
		inputs[4] = m_dummyInput.data();
		inputs[5] = m_dummyInput.data();
		inputs[6] = m_dummyInput.data();
		inputs[7] = m_dummyInput.data();

		outputs[1] = &m_audioOutputs[0].front();
		outputs[0] = &m_audioOutputs[1].front();
		outputs[3] = &m_audioOutputs[2].front();
		outputs[2] = &m_audioOutputs[3].front();
		outputs[5] = &m_audioOutputs[4].front();
		outputs[4] = &m_audioOutputs[5].front();
		outputs[7] = m_dummyOutput.data();
		outputs[6] = m_dummyOutput.data();
		outputs[9] = m_dummyOutput.data();
		outputs[8] = m_dummyOutput.data();
		outputs[11] = m_dummyOutput.data();
		outputs[10] = m_dummyOutput.data();

		while (_frames)
		{
			const auto processCount = std::min(_frames, static_cast<uint32_t>(1024));
			_frames -= processCount;

			if constexpr (g_useVoiceExpansion)
			{
				// VE mode: B→C and C→A routing is handled by continuous TX callbacks
				// (set up in initVoiceExpansion). processAudioOutput pops each frame
				// from A's output, extracts host audio, and routes A→B to complete
				// the ring.
				auto& rxInB = m_dsps[1].getPeriph().getEsai().getAudioInputs();

				esai.processAudioOutput<dsp56k::TWord>(processCount, [&](size_t _frame, dsp56k::Audio::TxFrame& _tx)
				{
					if(!_tx.empty())
					{
						outputs[0 ][_frame] = _tx[0][0];
						outputs[2 ][_frame] = _tx[0][1];
						outputs[4 ][_frame] = _tx[0][2];
						outputs[6 ][_frame] = _tx[0][3];
						outputs[8 ][_frame] = _tx[0][4];
						outputs[10][_frame] = _tx[0][5];

						if(_tx.size() >= 2)
						{
							outputs[ 1][_frame] = _tx[1][0];
							outputs[ 3][_frame] = _tx[1][1];
							outputs[ 5][_frame] = _tx[1][2];
							outputs[ 7][_frame] = _tx[1][3];
							outputs[ 9][_frame] = _tx[1][4];
							outputs[11][_frame] = _tx[1][5];
						}
					}

					// Route A→B to complete the ring
//					if (!rxInB.full())
					{
						dsp56k::Audio::RxFrame rx;
//						rx.resize(_tx.size());
						txToRx(_tx, rx);
						rxInB.push_back(std::move(rx));
					}
				});
			}
			else
			{
				esai.processAudioInputInterleaved(inputs, processCount, _latency);

				const auto requiredSize = processCount > 8 ? processCount - 8 : 0;

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
			}

			inputs[0] += processCount;
			inputs[1] += processCount;

			for (auto& output : outputs)
			{
				if (output)
					output += processCount;
			}
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

		if(m_dummyInput.size() < _frames)
			m_dummyInput.resize(_frames);
		if(m_dummyOutput.size() < _frames)
			m_dummyOutput.resize(_frames);
	}
}
