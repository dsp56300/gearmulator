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
	Hardware::Hardware(const ROM& _rom, const bool _voiceExpansion/* = false*/)
		: wLib::Hardware(44100)
		, m_rom(_rom)
		, m_useVoiceExpansion(_voiceExpansion)
		, m_uc(m_rom, _voiceExpansion)
		, m_midi(m_uc.getQSM(), 44100)
	{
		if(!m_rom.isValid())
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing);

		if (m_useVoiceExpansion)
		{
			m_dsps.push_back(std::make_unique<MqDsp>(*this, m_uc.getHdi08A().getHdi08(), 0));
			m_dsps.push_back(std::make_unique<MqDsp>(*this, m_uc.getHdi08B().getHdi08(), 1));
			m_dsps.push_back(std::make_unique<MqDsp>(*this, m_uc.getHdi08C().getHdi08(), 2));

			// On real VE hardware, Port C bit 4 (FST pin) is physically tied high on
			// the expansion board, telling DSP firmware this is a VE expansion DSP.
			// We must set both the external pin value (hostWrite) AND enable the pin
			// as GPIO input (setControl) before firmware boot. After reset, PCRC=0
			// means all pins are disconnected and dspRead returns 0 regardless of
			// hostWrite. Pre-enabling PCRC bit 4 ensures the firmware can read it
			// immediately during its early identification sequence.
			for (size_t i = 1; i < m_dsps.size(); ++i)
			{
				auto& portC = m_dsps[i]->getPeriph().getPortC();
				portC.setControl(0x10);   // Enable bit 4 as GPIO (not ESAI)
				portC.hostWrite(0x10);    // Set external pin value: bit 4 = VE expansion
			}

			// Set ESAI clock dividers immediately so frame rates are aligned from
			// the very first frame, including during boot.
			setupEsaiClockDividers();
		}
		else
		{
			m_dsps.push_back(std::make_unique<MqDsp>(*this, m_uc.getHdi08A().getHdi08(), 0));
		}

		m_uc.getPortF().setDirectionChangeCallback([&](const mc68k::Port& _port)
		{
			if(_port.getDirection() == 0xff)
				setGlobalDefaultParameters();
		});
	}

	Hardware::~Hardware()
	{
		for (auto & dsp : m_dsps)
		{
			dsp->getPeriph().getEsai().setCallback({});
			dsp->getPeriph().getEsai().setWriteTxCallback([](uint64_t&, const dsp56k::Audio::Frame<std::array<unsigned, 6>>&) {});
		}
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

		const auto& esai = m_dsps.front()->getPeriph().getEsai();

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
			m_dsps.front()->hdiSendIrqToDSP(dsp56k::Vba_NMI);

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

		m_dsps[1]->getPeriph().getPortC().hostWrite(0x10);	// set bit 4 of GPIO Port C, vexp DSPs are waiting for this
		m_dsps[2]->getPeriph().getPortC().hostWrite(0x10);	// set bit 4 of GPIO Port C, vexp DSPs are waiting for this

		auto& esaiA = m_dsps[0]->getPeriph().getEsai();
		auto& esaiB = m_dsps[1]->getPeriph().getEsai();
		auto& esaiC = m_dsps[2]->getPeriph().getEsai();

		// During boot, set empty callbacks on all ESAI interfaces to prevent DSP threads from blocking
		esaiA.setCallback([](dsp56k::Audio*) {});
		esaiB.setCallback([](dsp56k::Audio*) {});
		esaiC.setCallback([](dsp56k::Audio*) {});

		// Boot pump loop: route ESAI data along the chain (ADC→B→C→A→DAC).
		// Physical wiring: B's SDI0/RX0 is connected to the ADC (silence during
		// boot), B TX1→C RX1, C TX1→A RX1. There is no A→B ESAI link.
		// We feed empty frames to B (simulating ADC silence), route B→C and C→A,
		// and drain A's TX output. DSP A sends 0x654300 via HDI08 TX when ready.
		uint32_t loopCount = 0;
		uint32_t routedBC = 0, routedCA = 0;
		uint32_t lastStatusLog = 0;

		m_inBootPump = true;
		m_veResetCount = 0;

		while (true)
		{
			{
				// Mutex protects ESAI buffers from concurrent access during
				// DSP state reset (resetState drains/primes ESAI buffers)
				std::lock_guard lock(m_esaiBootMutex);

				auto& txOutA = esaiA.getAudioOutputs();
				auto& txOutB = esaiB.getAudioOutputs();
				auto& txOutC = esaiC.getAudioOutputs();
				auto& rxInB  = esaiB.getAudioInputs();
				auto& rxInC  = esaiC.getAudioInputs();
				auto& rxInA  = esaiA.getAudioInputs();

				// ADC → B: feed silence (empty frames) to B's RX, simulating the ADC
				if (rxInB.empty())
					rxInB.push_back({});

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

				// Drain A's TX output (goes to DAC on real hardware, nowhere useful during boot)
				while (!txOutA.empty())
					txOutA.pop_front();
			}

			++loopCount;

			// Periodic status logging
			if (loopCount - lastStatusLog >= 5000000)
			{
				lastStatusLog = loopCount;
				LOG("Boot pump status: loop=" << loopCount
					<< " BC=" << routedBC << " CA=" << routedCA
					<< " resets=" << m_veResetCount
					<< " bootCompleted=" << m_bootCompleted
					<< " threads=" << m_dsps[0]->hasThread() << m_dsps[1]->hasThread() << m_dsps[2]->hasThread());
			}

			// Exit when UC firmware signals boot complete AND the chain has
			// routed data (C→A confirms B→C→A path is working)
			if (m_bootCompleted && routedCA > 0)
				break;

			std::this_thread::yield();
		}

		LOG("Voice Expansion initialization completed after " << m_veResetCount << " resets");

		// Block further DSP resets BEFORE clearing boot pump flag.
		// Otherwise the UC thread can see m_inBootPump=false while
		// m_voiceExpansionReady is still false and do a direct reset.
		m_voiceExpansionReady = true;
		m_inBootPump = false;

		// ESAI clock dividers were already set in the Hardware constructor
		// (before any DSP threads started) to ensure frame rates are aligned
		// from the very first frame.

		// Drain any stale boot-time output
		for (auto& dsp : m_dsps)
		{
			auto& out = dsp->getPeriph().getEsai().getAudioOutputs();
			auto& in  = dsp->getPeriph().getEsai().getAudioInputs();
			while (!out.empty() || !in.empty())
				out.pop_front();
		}

		// Set up A's ESAI callback for frame counting + CV notification
		setupEsaiListener();

		// Post-boot ESAI chain routing: B→C and C→A via direct TX callbacks.
		// These fire from the DSP thread when a TX frame completes, bypassing
		// the TX output ring buffer entirely. Audio input is fed to DSP B in
		// processAudio (ADC→B→C→A→DAC).

		auto& rxInC = m_dsps[2]->getPeriph().getEsai().getAudioInputs();
		auto& rxInA = m_dsps[0]->getPeriph().getEsai().getAudioInputs();

		m_dsps[1]->getPeriph().getEsai().setWriteTxCallback([&rxInC](uint64_t& _frameIndex, const dsp56k::Audio::TxFrame& _tx)
		{
			dsp56k::Audio::RxFrame rx;
			txToRx(_tx, rx);
			rxInC.push_back(std::move(rx));
			++_frameIndex;
		});

		m_dsps[2]->getPeriph().getEsai().setWriteTxCallback([&rxInA](uint64_t& _frameIndex, const dsp56k::Audio::TxFrame& _tx)
		{
			dsp56k::Audio::RxFrame rx;
			txToRx(_tx, rx);
			rxInA.push_back(std::move(rx));
			++_frameIndex;
		});

		// prefill just a few samples to initiate execution, DSP B is the one needing input as its the one receiving the ADC signal
		m_dsps[1]->getPeriph().getEsai().writeEmptyAudioIn(4);

		/*
		// Dump all DSP P memories as disassembly
		const char* dspNames[] = {"A", "B", "C"};
		for (uint32_t i = 0; i < m_dsps.size(); ++i)
		{
			const auto filename = std::string("e:\\mqDsp") + dspNames[i] + "_P.asm";
			const auto& mem = m_dsps[i]->dsp().memory();
			mem.saveAssembly(filename.c_str(), 0, MqDsp::g_pMemSize, false, false, m_dsps[i]->dsp().getPeriph(0), m_dsps[i]->dsp().getPeriph(1));
			LOG("Saved DSP " << dspNames[i] << " P memory to " << filename);
		}
		*/
	}

	void Hardware::setupEsaiClockDividers()
	{
		if (!m_useVoiceExpansion)
			return;

		// DSP A: txWC=1 (2-slot TX, slow), rxWC=31 (32-slot RX, fast)
		// DSP B: txWC=31 (32-slot TX, fast), rxWC=1 (2-slot RX, slow)
		// DSP C: txWC=31, rxWC=31 (both 32-slot, no adjustment needed)
		m_dsps[0]->getPeriph().getEsaiClock().setEsaiDivider(&m_dsps[0]->getPeriph().getEsai(), 15, 0);
		m_dsps[1]->getPeriph().getEsaiClock().setEsaiDivider(&m_dsps[1]->getPeriph().getEsai(), 0, 15);
		m_dsps[2]->getPeriph().getEsaiClock().setEsaiDivider(&m_dsps[2]->getPeriph().getEsai(), 0, 0);
	}

	void Hardware::setupEsaiListener()
	{
		auto& esaiA = m_dsps.front()->getPeriph().getEsai();

		esaiA.setCallback([&](dsp56k::Audio*)
		{
			onEsaiCallback(esaiA);
		});
	}

	void Hardware::processUcCycle()
	{
		syncUcToDSP();

		const auto deltaCycles = m_uc.exec();
		if(m_esaiFrameIndex > 0)
			m_remainingUcCycles -= static_cast<int64_t>(deltaCycles);

		for (size_t i = 0; i < m_dsps.size(); ++i)
			m_dsps[i]->transferHostFlagsUc2Dsdp();

		hdiProcessUCtoDSPNMIIrq();

		for (auto& dsp : m_dsps)
			dsp->hdiTransferDSPtoUC();

		if(m_uc.requestDSPReset() && !m_voiceExpansionReady)
		{
			if(!m_dspResetPending)
			{
				LOG("DSP reset requested (count " << (m_veResetCount + 1) << ")");

				// Phase 1: Terminate DSP threads WITHOUT holding the mutex.
				// The boot pump continues draining ESAI outputs, which unblocks
				// any DSP thread stuck in Audio::writeTXimpl::waitNotFull().
				for (auto& dsp : m_dsps)
					dsp->terminateThread();

				// Phase 2: Lock mutex and reset DSP state (ESAI, P-memory, etc.)
				// The boot pump waits on the mutex during this brief phase.
				{
					std::lock_guard lock(m_esaiBootMutex);
					for (auto& dsp : m_dsps)
						dsp->resetState();

					// Re-apply ESAI clock dividers cleared by resetHW()
					setupEsaiClockDividers();
				}

				++m_veResetCount;
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
		LOG("Boot completed (m_bootCompleted=true)");

		/*
		if (m_dsps.size() == 1)
		{
			const auto filename = std::string("e:\\mqDspSingle_P.asm");
			const auto& mem = m_dsps[0]->dsp().memory();
			mem.saveAssembly(filename.c_str(), 0, MqDsp::g_pMemSize, false, false, m_dsps[0]->dsp().getPeriph(0), m_dsps[0]->dsp().getPeriph(1));
		}
		*/
	}

	void Hardware::processAudio(uint32_t _frames, uint32_t _latency)
	{
		ensureBufferSize(_frames);

		if(m_esaiFrameIndex == 0)
			return;

		m_midi.process(_frames);

		m_processAudio = true;

		auto& esai = m_dsps.front()->getPeriph().getEsai();

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

			if (m_useVoiceExpansion)
			{
				// VE topology is a chain, not a ring: ADC→B→C→A→DAC
				// Feed host audio input to DSP B (the expansion DSP whose
				// SDI0/RX0 is wired to the ADC on real hardware).
				m_dsps[1]->getPeriph().getEsai().processAudioInputInterleaved(inputs, processCount, _latency);

				// B→C and C→A routing is handled by continuous TX callbacks
				// (set up in initVoiceExpansion). Extract host audio output
				// from DSP A's TX.
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
