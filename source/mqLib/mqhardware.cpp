#include "mqhardware.h"

#include "synthLib/midiBufferParser.h"
#include "synthLib/deviceException.h"

#include <algorithm>
#include <array>
#include <cstdlib>	// getenv
#include <cstring>	// memcpy
#include <cstdio>

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

	uint32_t s_bcFrameCount = 0;
	uint32_t s_caFrameCount = 0;
	uint32_t s_abFrameCount = 0;
	uint32_t s_bcDrops = 0;
	uint32_t s_caDrops = 0;
	uint32_t s_abDrops = 0;

	struct ExpansionSample
	{
		int32_t left = 0;
		int32_t right = 0;
	};
	dsp56k::RingBuffer<ExpansionSample, 4096, false> s_expansionMixB;
	dsp56k::RingBuffer<ExpansionSample, 4096, false> s_expansionMixC;

	// Snapshot of B's last per-slot TX1 values for comparison with C
	std::array<uint32_t, 20> s_lastBSlots = {};

	// Per-source audio stats
	uint32_t s_aNonZero = 0, s_bNonZero = 0, s_cNonZero = 0;
	uint32_t s_aTotal = 0, s_bTotal = 0, s_cTotal = 0;

	// Sample-and-hold for isolated mode: last B/C stereo pair at A's rate
	ExpansionSample s_holdB{0, 0};
	ExpansionSample s_holdC{0, 0};

	// Isolated DSP mode: no inter-DSP routing, each DSP captures independently
	const bool s_isolateDSPs = (std::getenv("MQ_VE_ISOLATE") != nullptr);
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
		m_dsps.front().getPeriph().getEsai().setCallback({}, 0);
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

		// In real hardware, all 3 DSPs share the same ESAI bit-clock so
		// every frame period contains the MAXIMUM slot count (32).  DSPs
		// with fewer active slots simply idle for the remaining slots.
		// In the emulator each DSP has its own EsxiClock, so we must add
		// per-direction dividers to keep the frame rates aligned:
		//   baseCPS = normalCPS * masterTxSlots / maxSlots
		//   A: txDiv=15 (2-slot TX, slow down 16×), rxDiv=0 (32-slot RX, full speed)
		//   B: txDiv=0  (32-slot TX, full speed),  rxDiv=15 (2-slot RX, slow down 16×)
		//   C: txDiv=0  (32-slot TX, full speed),  rxDiv=0  (32-slot RX, full speed)
		{
			auto& clockA = m_dsps[0].getPeriph().getEsaiClock();
			auto& clockB = m_dsps[1].getPeriph().getEsaiClock();
			auto& clockC = m_dsps[2].getPeriph().getEsaiClock();
			auto& esaiA  = m_dsps[0].getPeriph().getEsai();
			auto& esaiB  = m_dsps[1].getPeriph().getEsai();
			auto& esaiC  = m_dsps[2].getPeriph().getEsai();

			clockA.setEsaiDivider(&esaiA, 15, 0);   // A: TX÷16, RX÷1
			clockB.setEsaiDivider(&esaiB, 0, 15);    // B: TX÷1, RX÷16
			clockC.setEsaiDivider(&esaiC, 0, 0);     // C: TX÷1, RX÷1

			LOG("Voice Expansion: ESAI clock dividers set — A:tx15/rx0  B:tx0/rx15  C:tx0/rx0");
		}

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

		if (s_isolateDSPs)
		{
			LOG("Voice Expansion: ISOLATED DSP mode — ring routes zeroes");
			fprintf(stderr, "[VE] ISOLATED DSP mode enabled\n");
			fflush(stderr);

			// Isolated mode: keep the ring topology (B→C, C→A) for synchronization
			// but route zeroes instead of actual audio. Each callback captures TX for
			// WAV output, then feeds an empty RX frame to the next DSP.

			// B callback: capture TX, route zeroes to C
			m_dsps[1].getPeriph().getEsai().setCallback([this](dsp56k::Audio* _audio)
			{
				auto& txOut = _audio->getAudioOutputs();
				auto& rxInC = m_dsps[2].getPeriph().getEsai().getAudioInputs();
				while (!txOut.empty())
				{
					if (rxInC.full())
					{
						++s_bcDrops;
						txOut.pop_front();
						continue;
					}
					auto& tx = txOut.front();
					++s_bcFrameCount;

					// Capture B's audio into expansion ring buffer
					if (!s_expansionMixB.full())
					{
						int32_t left = 0, right = 0;
						const uint32_t nSlots = std::min(tx.size(), 20u);
						for (uint32_t s = 0; s < nSlots; s += 2)
						{
							left += static_cast<int32_t>(tx[s][1] << 8) >> 8;
							if (s + 1 < nSlots)
								right += static_cast<int32_t>(tx[s+1][1] << 8) >> 8;
						}
						s_expansionMixB.push_back({left, right});
					}

					// Periodic slot analysis
					if (s_bcFrameCount % 10000 == 1)
					{
						const uint32_t nSlots = std::min(tx.size(), 32u);
						uint32_t nzCount = 0;
						for (uint32_t s = 0; s < nSlots; ++s)
							if (tx[s][1] != 0 && tx[s][1] != 0xcdcdcd) ++nzCount;
						if (nzCount > 0)
						{
							fprintf(stderr, "[VE-ISO-B] frame=%u slots=%u nz=%u:", s_bcFrameCount, nSlots, nzCount);
							for (uint32_t s = 0; s < nSlots; ++s)
								if (tx[s][1] != 0 && tx[s][1] != 0xcdcdcd)
									fprintf(stderr, " s%u=%06x", s, tx[s][1] & 0xFFFFFF);
							fprintf(stderr, "\n");
							fflush(stderr);
						}
					}

					// Route zeroes to C (not B's actual audio)
					dsp56k::Audio::RxFrame rx{};
					txOut.pop_front();
					rxInC.push_back(std::move(rx));
				}
			}, 0);

			// C callback: capture TX, route zeroes to A
			m_dsps[2].getPeriph().getEsai().setCallback([this](dsp56k::Audio* _audio)
			{
				auto& txOut = _audio->getAudioOutputs();
				auto& rxInA = m_dsps[0].getPeriph().getEsai().getAudioInputs();
				while (!txOut.empty())
				{
					if (rxInA.full())
					{
						++s_caDrops;
						txOut.pop_front();
						continue;
					}
					auto& tx = txOut.front();
					++s_caFrameCount;

					// Capture C's audio into expansion ring buffer
					if (!s_expansionMixC.full())
					{
						int32_t left = 0, right = 0;
						const uint32_t nSlots = std::min(tx.size(), 20u);
						for (uint32_t s = 0; s < nSlots; s += 2)
						{
							left += static_cast<int32_t>(tx[s][1] << 8) >> 8;
							if (s + 1 < nSlots)
								right += static_cast<int32_t>(tx[s+1][1] << 8) >> 8;
						}
						s_expansionMixC.push_back({left, right});
					}

					// Periodic slot analysis
					if (s_caFrameCount % 10000 == 1)
					{
						const uint32_t nSlots = std::min(tx.size(), 32u);
						uint32_t nzCount = 0;
						for (uint32_t s = 0; s < nSlots; ++s)
							if (tx[s][1] != 0 && tx[s][1] != 0xcdcdcd) ++nzCount;
						if (nzCount > 0)
						{
							fprintf(stderr, "[VE-ISO-C] frame=%u slots=%u nz=%u:", s_caFrameCount, nSlots, nzCount);
							for (uint32_t s = 0; s < nSlots; ++s)
								if (tx[s][1] != 0 && tx[s][1] != 0xcdcdcd)
									fprintf(stderr, " s%u=%06x", s, tx[s][1] & 0xFFFFFF);
							fprintf(stderr, "\n");
							fflush(stderr);
						}
					}

					// Route zeroes to A (not C's actual audio)
					dsp56k::Audio::RxFrame rx{};
					txOut.pop_front();
					rxInA.push_back(std::move(rx));
				}
			}, 0);
		}
		else
		{
			// Normal ring routing: B→C, C→A via TX callbacks.
			// A→B handled in processAudio alongside host audio extraction.

			m_dsps[1].getPeriph().getEsai().setCallback([this](dsp56k::Audio* _audio)
			{
				auto& txOut = _audio->getAudioOutputs();
				auto& rxIn  = m_dsps[2].getPeriph().getEsai().getAudioInputs();
				while (!txOut.empty())
				{
					if (rxIn.full())
					{
						++s_bcDrops;
						txOut.pop_front();
						continue;
					}
					auto& tx = txOut.front();
					++s_bcFrameCount;

					if (s_bcFrameCount % 5000 == 0)
					{
						uint32_t nonZeroSlots = 0;
						int64_t sumTx1 = 0;
						for (uint32_t s = 0; s < tx.size(); ++s)
						{
							if (tx[s][1] != 0) ++nonZeroSlots;
							const int32_t val = static_cast<int32_t>(tx[s][1] << 8) >> 8;
							sumTx1 += (val < 0 ? -val : val);
						}
						fprintf(stderr, "[VE-BC] frame=%u nz=%u sum=%lld rxCFill=%u\n",
							s_bcFrameCount, nonZeroSlots, sumTx1,
							static_cast<uint32_t>(rxIn.size()));
						if (s_bcFrameCount % 100000 == 0)
						{
							fprintf(stderr, "[VE-BC-SLOTS] s0-19:");
							for (uint32_t s = 0; s < 20 && s < tx.size(); ++s)
							{
								fprintf(stderr, " %06x", tx[s][1] & 0xFFFFFF);
								s_lastBSlots[s] = tx[s][1] & 0xFFFFFF;
							}
							fprintf(stderr, "\n");
						}
						fflush(stderr);
					}

					if (!s_expansionMixB.full())
					{
						const uint32_t activeSlots = std::min(tx.size(), 20u);
						int32_t leftSum = 0, rightSum = 0;
						for (uint32_t s = 0; s < activeSlots; s += 2)
						{
							leftSum += static_cast<int32_t>(tx[s][1] << 8) >> 8;
							if (s + 1 < activeSlots)
								rightSum += static_cast<int32_t>(tx[s+1][1] << 8) >> 8;
						}
						s_expansionMixB.push_back({leftSum, rightSum});
					}

					dsp56k::Audio::RxFrame rx;
					txToRx(tx, rx);
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
					if (rxIn.full())
					{
						++s_caDrops;
						txOut.pop_front();
						continue;
					}
					auto& tx = txOut.front();
					++s_caFrameCount;

					if (!s_expansionMixC.full())
					{
						const uint32_t activeSlots = std::min(tx.size(), 20u);
						int32_t leftSum = 0, rightSum = 0;
						for (uint32_t s = 0; s < activeSlots; s += 2)
						{
							leftSum += static_cast<int32_t>(tx[s][1] << 8) >> 8;
							if (s + 1 < activeSlots)
								rightSum += static_cast<int32_t>(tx[s+1][1] << 8) >> 8;
						}
						s_expansionMixC.push_back({leftSum, rightSum});
					}

					if (s_caFrameCount % 5000 == 0)
					{
						uint32_t nonZeroSlots = 0;
						int64_t sumTx1 = 0;
						for (uint32_t s = 0; s < tx.size(); ++s)
						{
							if (tx[s][1] != 0) ++nonZeroSlots;
							const int32_t val = static_cast<int32_t>(tx[s][1] << 8) >> 8;
							sumTx1 += (val < 0 ? -val : val);
						}
						fprintf(stderr, "[VE-CA] frame=%u nz=%u sum=%lld rxAFill=%u\n",
							s_caFrameCount, nonZeroSlots, sumTx1,
							static_cast<uint32_t>(rxIn.size()));
						if (s_caFrameCount % 100000 == 0)
						{
							fprintf(stderr, "[VE-CA-SLOTS] s0-19:");
							uint32_t matchCount = 0;
							for (uint32_t s = 0; s < 20 && s < tx.size(); ++s)
							{
								const auto v = tx[s][1] & 0xFFFFFF;
								fprintf(stderr, " %06x", v);
								if (v == s_lastBSlots[s] && v != 0)
									++matchCount;
							}
							fprintf(stderr, "\n[VE-COMPARE] B==C nonzero matches: %u/20\n", matchCount);
						}
						fflush(stderr);
					}

					dsp56k::Audio::RxFrame rx;
					txToRx(tx, rx);
					txOut.pop_front();
					rxIn.push_back(std::move(rx));
				}
			}, 0);
		}
	}

	void Hardware::setupEsaiListener()
	{
		auto& esaiA = m_dsps.front().getPeriph().getEsai();

#if MQ_VOICE_EXPANSION
		if(m_dsps.size() >= 3)
		{
			// VE mode: A callback increments frame index.
			esaiA.setCallback([&, this](dsp56k::Audio*)
			{
				onEsaiCallback(esaiA);
			}, 0);
		}
		else
#endif
		{
			esaiA.setCallback([&](dsp56k::Audio*)
			{
				onEsaiCallback(esaiA);
			}, 0);
		}
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
		outputs[7] = &m_audioOutputs[6].front();
		outputs[6] = &m_audioOutputs[7].front();
		outputs[9] = &m_audioOutputs[8].front();
		outputs[8] = &m_audioOutputs[9].front();
		outputs[11] = &m_audioOutputs[10].front();
		outputs[10] = &m_audioOutputs[11].front();
		// 12-15: B/C expansion stereo pairs (no swap needed)
		outputs[12] = &m_audioOutputs[12].front();
		outputs[13] = &m_audioOutputs[13].front();
		outputs[14] = &m_audioOutputs[14].front();
		outputs[15] = &m_audioOutputs[15].front();

		while (_frames)
		{
			const auto processCount = std::min(_frames, static_cast<uint32_t>(1024));
			_frames -= processCount;

			if constexpr (g_useVoiceExpansion)
			{
				// Periodic diagnostics for A's RX input queue
				static uint32_t s_veLogCounter = 0;
				if (++s_veLogCounter % 500 == 0)
				{
					auto& rxInA = m_dsps[0].getPeriph().getEsai().getAudioInputs();
					auto& hdiB = m_dsps[1].hdi08();
					auto& hdiC = m_dsps[2].hdi08();
					fprintf(stderr, "[VE-DIAG] A-rxFill=%u AB=%u BC=%u CA=%u hdiB-rx=%u hdiC-rx=%u expB=%u expC=%u\n",
						static_cast<uint32_t>(rxInA.size()),
						s_abFrameCount, s_bcFrameCount, s_caFrameCount,
						static_cast<uint32_t>(hdiB.rxData().size()),
						static_cast<uint32_t>(hdiC.rxData().size()),
						static_cast<uint32_t>(s_expansionMixB.size()),
						static_cast<uint32_t>(s_expansionMixC.size()));
					fflush(stderr);

					// One-time ESAI + DMA register dump for all 3 DSPs
					if (s_veLogCounter == 500)
					{
						for (uint32_t d = 0; d < 3; ++d)
						{
							auto& esaiD = m_dsps[d].getPeriph().getEsai();
							const auto tcr = esaiD.readTransmitControlRegister();
							const auto rcr = esaiD.readReceiveControlRegister();
							const uint32_t tem = tcr & 0x3F;
							const uint32_t rem = rcr & 0x0F;
							fprintf(stderr, "[VE-ESAI-%c] TCR=%06x RCR=%06x TEM=%02x REM=%x txWC=%u rxWC=%u\n",
								'A'+d, tcr, rcr, tem, rem, esaiD.getTxWordCount(), esaiD.getRxWordCount());
							auto& dma = m_dsps[d].getPeriph().getDMA();
							for (uint32_t ch = 0; ch < 6; ++ch)
							{
								const auto dcr = dma.getDCR(ch);
								if (dcr == 0) continue;
								fprintf(stderr, "[VE-DMA-%c-ch%u] DCR=%06x DSR=%06x DDR=%06x DCO=%06x\n",
									'A'+d, ch, dcr, dma.getDSR(ch), dma.getDDR(ch), dma.getDCO(ch));
							}
						}
						fflush(stderr);
					}

					// Periodic memory probes (every ~2 min)
					if (s_veLogCounter % 5000 == 0)
					{
						for (uint32_t dd = 1; dd < 3; ++dd)
						{
							auto& ddma = m_dsps[dd].getPeriph().getDMA();
							const auto txSrc = ddma.getDSR(0);
							uint32_t nz = 0;
							int64_t asum = 0;
							for (uint32_t i = 0; i < 32; ++i)
							{
								const auto v = m_dsps[dd].dsp().memory().get(dsp56k::MemArea_X, txSrc + i);
								if (v != 0) ++nz;
								const int32_t sv = static_cast<int32_t>(v << 8) >> 8;
								asum += (sv < 0 ? -sv : sv);
							}
							fprintf(stderr, "[VE-TXMEM-%c] src=X:%06x nz=%u sum=%lld\n", 'A'+dd, txSrc, nz, asum);
						}
						{
							auto& adma = m_dsps[0].getPeriph().getDMA();
							const auto rxDst = adma.getDDR(4);
							uint32_t nz = 0;
							int64_t asum = 0;
							for (uint32_t i = 0; i < 64; ++i)
							{
								const auto v = m_dsps[0].dsp().memory().get(dsp56k::MemArea_X, rxDst + i);
								if (v != 0) ++nz;
								const int32_t sv = static_cast<int32_t>(v << 8) >> 8;
								asum += (sv < 0 ? -sv : sv);
							}
							fprintf(stderr, "[VE-RXMEM-A] dst=X:%06x nz=%u/64 sum=%lld\n", rxDst, nz, asum);
						}
						fflush(stderr);
					}
				}

				// VE mode: B→C and C→A routing is handled by continuous TX callbacks
				// (set up in initVoiceExpansion). processAudioOutput pops each frame
				// from A's output, extracts host audio, and routes A→B to complete
				// the ring.
				auto& rxInB = m_dsps[1].getPeriph().getEsai().getAudioInputs();

				esai.processAudioOutput<dsp56k::TWord>(processCount, [&](size_t _frame, dsp56k::Audio::TxFrame& _tx)
				{
					if(!_tx.empty())
					{
						// Log A's TX output periodically
						static uint32_t s_aTxLogCounter = 0;
						if (++s_aTxLogCounter % 50000 == 0)
						{
							fprintf(stderr, "[VE-ATX] slots=%u s0=[%06x,%06x,%06x] s1=[%06x,%06x,%06x]\n",
								static_cast<uint32_t>(_tx.size()),
								_tx[0][0] & 0xFFFFFF, _tx[0][1] & 0xFFFFFF, _tx[0][2] & 0xFFFFFF,
								_tx.size() >= 2 ? _tx[1][0] & 0xFFFFFF : 0,
								_tx.size() >= 2 ? _tx[1][1] & 0xFFFFFF : 0,
								_tx.size() >= 2 ? _tx[1][2] & 0xFFFFFF : 0);
							fflush(stderr);
						}

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

						// Write expansion audio — pop from B/C ring buffers (ring-paced, 1:1 with A)
						{
							ExpansionSample bVal{};
							ExpansionSample cVal{};
							if (!s_expansionMixB.empty())
								bVal = s_expansionMixB.pop_front();
							if (!s_expansionMixC.empty())
								cVal = s_expansionMixC.pop_front();

							// Track per-source audio stats
							const int32_t aLeft  = static_cast<int32_t>(outputs[0][_frame] << 8) >> 8;
							const int32_t aRight = static_cast<int32_t>(outputs[1][_frame] << 8) >> 8;
							++s_aTotal; ++s_bTotal; ++s_cTotal;
							if (aLeft != 0 || aRight != 0) ++s_aNonZero;
							if (bVal.left != 0 || bVal.right != 0) ++s_bNonZero;
							if (cVal.left != 0 || cVal.right != 0) ++s_cNonZero;

							// Log individual B/C contributions + per-source stats periodically
							static uint32_t s_expLogCounter = 0;
							if (++s_expLogCounter % 100000 == 0)
							{
								fprintf(stderr, "[VE-EXPMIX] B=(%d,%d) C=(%d,%d) expB=%u expC=%u\n",
									bVal.left, bVal.right, cVal.left, cVal.right,
									static_cast<uint32_t>(s_expansionMixB.size()),
									static_cast<uint32_t>(s_expansionMixC.size()));
								fprintf(stderr, "[VE-SRCSTAT] A=%u/%u(%.0f%%) B=%u/%u(%.0f%%) C=%u/%u(%.0f%%)\n",
									s_aNonZero, s_aTotal, s_aTotal ? 100.0*s_aNonZero/s_aTotal : 0.0,
									s_bNonZero, s_bTotal, s_bTotal ? 100.0*s_bNonZero/s_bTotal : 0.0,
									s_cNonZero, s_cTotal, s_cTotal ? 100.0*s_cNonZero/s_cTotal : 0.0);
								fflush(stderr);
							}

							// B expansion on outputs 12/13
							outputs[12][_frame] = static_cast<dsp56k::TWord>(std::clamp(bVal.left,  -0x800000, 0x7FFFFF)) & 0xFFFFFF;
							outputs[13][_frame] = static_cast<dsp56k::TWord>(std::clamp(bVal.right, -0x800000, 0x7FFFFF)) & 0xFFFFFF;
							// C expansion on outputs 14/15
							outputs[14][_frame] = static_cast<dsp56k::TWord>(std::clamp(cVal.left,  -0x800000, 0x7FFFFF)) & 0xFFFFFF;
							outputs[15][_frame] = static_cast<dsp56k::TWord>(std::clamp(cVal.right, -0x800000, 0x7FFFFF)) & 0xFFFFFF;
						}
					}

					// Route A→B: zeroes in isolated mode, actual data otherwise
					++s_abFrameCount;
					if (!rxInB.full())
					{
						dsp56k::Audio::RxFrame rx{};
						if (!s_isolateDSPs)
							txToRx(_tx, rx);
						rxInB.push_back(std::move(rx));
					}
					else
					{
						++s_abDrops;
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

		if(m_dummyInput.size() < _frames)
			m_dummyInput.resize(_frames);
		if(m_dummyOutput.size() < _frames)
			m_dummyOutput.resize(_frames);
	}
}
