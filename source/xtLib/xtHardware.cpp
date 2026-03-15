#include "xtHardware.h"

#include "synthLib/midiBufferParser.h"
#include "synthLib/deviceException.h"

#include <cstring>	// memcpy

#include "xtRomLoader.h"

namespace xt
{
	Rom initializeRom(const std::vector<uint8_t>& _romData, const std::string& _romName)
	{
		if(_romData.empty())
			return RomLoader::findROM();
		return Rom{_romName, _romData};
	}

	Hardware::Hardware(const std::vector<uint8_t>& _romData, const std::string& _romName, const bool _voiceExpansion/* = false*/)
		: wLib::Hardware(40000)
		, m_rom(initializeRom(_romData, _romName))
		, m_useVoiceExpansion(_voiceExpansion)
		, m_uc(m_rom, _voiceExpansion)
		, m_midi(m_uc)
	{
		if(!m_rom.isValid())
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing);

		if (m_useVoiceExpansion)
		{
			// Expansion DSP index mapping (verified from actual XT ROM dumps):
			// idx 0 = HDI08B (0xFC000) = exp1: voices, ESSI0 RX (ext audio) + ESSI1 TX/RX
			// idx 1 = HDI08C (0xFD000) = exp2: voices + mixing, ESSI1 only (no ESSI0)
			// idx 2 = HDI08A (0xFE000) = exp3: effects + final output, ESSI0 TX + ESSI1 TX/RX
			m_dsps.push_back(std::make_unique<DSP>(*this, m_uc.getHdi08B().getHdi08(), 0, true));
			m_dsps.push_back(std::make_unique<DSP>(*this, m_uc.getHdi08C().getHdi08(), 1, true));
			m_dsps.push_back(std::make_unique<DSP>(*this, m_uc.getHdi08A().getHdi08(), 2, true));
		}
		else
		{
			m_dsps.push_back(std::make_unique<DSP>(*this, m_uc.getHdi08A().getHdi08(), 0));
		}
	}

	Hardware::~Hardware()
	{
		for(const auto& dsp : m_dsps)
		{
			dsp->getPeriph().getEssi0().setCallback({},0);
			dsp->getPeriph().getEssi1().setCallback({},0);
		}
	}

	void Hardware::process()
	{
		processUcCycle();
	}
	
	void Hardware::resetMidiCounter()
	{
		// wait for DSP to enter blocking state
		const auto& esai = m_dsps[getMainDspIdx()]->getPeriph().getEssi0();

		auto& inputs = esai.getAudioInputs();
		auto& outputs = esai.getAudioOutputs();

		while(inputs.size() > 2 && !outputs.full())
			std::this_thread::yield();

		m_midiOffsetCounter = 0;
	}

	namespace
	{
		// Convert a TX frame to an RX frame for ESSI routing between DSPs.
		// TX slots have 6 words, RX slots have 4 — copy the first 4 words of each slot.
		void txToRx(const dsp56k::Audio::TxFrame& _tx, dsp56k::Audio::RxFrame& _rx)
		{
			_rx.resize(_tx.size());
			for (uint32_t s = 0; s < _tx.size(); ++s)
			{
				for (uint32_t w = 0; w < dsp56k::Audio::RxRegisterCount; ++w)
				{
//					LOG("Routing ESSI1 frame: slot " << s << " word " << w << " value " << std::hex << _tx[s][w]);
					_rx[s][w] = _tx[s][w];
				}
			}
		}
	}

	void Hardware::initVoiceExpansion()
	{
		if (m_dsps.size() < 3)
		{
			setupEsaiListener();
			return;
		}

		const auto mainDspIdx = getMainDspIdx();

		// During boot, set empty callbacks on all ESSI interfaces to prevent DSP threads from blocking
		for (uint32_t i = 0; i < 3; ++i)
		{
			m_dsps[i]->getPeriph().getEssi0().setCallback([](dsp56k::Audio*) {}, 0);
			m_dsps[i]->getPeriph().getEssi1().setCallback([](dsp56k::Audio*) {}, 0);
		}

		// Boot pump loop: DSPs sync via ESSI1 ring (DSP2 TX→DSP0 RX→DSP0 TX→DSP1 RX→DSP1 TX→DSP2 RX).
		// DSP2 (exp3, idx 2) sends magic $535400 on ESSI1 TX, DSP0/DSP1 echo it forward.
		// We must route ESSI1 data between DSPs for the sync handshake to complete.
		auto& mainEssi0 = m_dsps[mainDspIdx]->getPeriph().getEssi0();

		// ESSI1 ring connections: [from].TX → [to].RX
		constexpr uint32_t essi1From[] = {2, 0, 1};  // TX source DSP index
		constexpr uint32_t essi1To[]   = {0, 1, 2};  // RX destination DSP index

		while (mainEssi0.getAudioOutputs().empty())
		{
			// Route ESSI1 data around the ring
			for (int leg = 0; leg < 3; ++leg)
			{
				auto& txOut = m_dsps[essi1From[leg]]->getPeriph().getEssi1().getAudioOutputs();
				auto& rxIn  = m_dsps[essi1To[leg]]->getPeriph().getEssi1().getAudioInputs();

				while (!txOut.empty() && !rxIn.full())
				{
					dsp56k::Audio::RxFrame rx;
					txToRx(txOut.pop_front(), rx);
					rxIn.push_back(std::move(rx));
				}
			}

			// Feed ESSI0 inputs (DSP0 uses ESSI0 RX for external audio, feed silence)
			for (uint32_t i = 0; i < 3; ++i)
			{
				auto& in0 = m_dsps[i]->getPeriph().getEssi0().getAudioInputs();
				if (in0.empty())
					in0.push_back({});
			}

			// Drain ESSI0 outputs from non-main DSPs to prevent overflow
			for (uint32_t i = 0; i < 3; ++i)
			{
				if (i != mainDspIdx)
				{
					auto& out0 = m_dsps[i]->getPeriph().getEssi0().getAudioOutputs();
					while (out0.size() > 32)
						out0.pop_front();
				}
			}

			std::this_thread::yield();
		}

		LOG("Voice Expansion boot completed, main DSP index " << mainDspIdx);

		// Prime each DSP with minimal input to prevent blocking, then drain outputs
		for (auto& dsp : m_dsps)
		{
			dsp->getPeriph().getEssi0().writeEmptyAudioIn(8);
			dsp->getPeriph().getEssi1().writeEmptyAudioIn(8 * 8);

			while (!dsp->getPeriph().getEssi0().getAudioOutputs().empty())
				dsp->getPeriph().getEssi0().getAudioOutputs().pop_front();
			while (!dsp->getPeriph().getEssi1().getAudioOutputs().empty())
				dsp->getPeriph().getEssi1().getAudioOutputs().pop_front();
		}

		mainEssi0.setCallback([this, &mainEssi0](dsp56k::Audio*)
		{
			m_bootCompleted = true;
			onEsaiCallback(mainEssi0);
		}, 0);

		// Real-time ESSI1 ring routing via TX callbacks.
		constexpr uint32_t essi1RxDst[] = {1, 2, 0};  // DSP[i] TX routes to DSP[essi1RxDst[i]] RX
		for (uint32_t i = 0; i < getDspCount(); ++i)
		{
			auto& srcEssi1 = m_dsps[i]->getPeriph().getEssi1();
			auto& dstEssi1 = m_dsps[essi1RxDst[i]]->getPeriph().getEssi1();

			srcEssi1.setCallback([&srcEssi1, &dstEssi1](dsp56k::Audio*)
			{
				auto& txOut = srcEssi1.getAudioOutputs();
				auto& rxIn = dstEssi1.getAudioInputs();
				while (!txOut.empty() && !rxIn.full())
				{
					dsp56k::Audio::RxFrame rx;
					txToRx(txOut.pop_front(), rx);
					rxIn.push_back(std::move(rx));
				}
			}, 0);
		}
	}

	void Hardware::setupEsaiListener()
	{
		auto& esaiA = m_dsps.front()->getPeriph().getEssi0();

		esaiA.setCallback([&](dsp56k::Audio*)
		{
			m_bootCompleted = true;

			onEsaiCallback(esaiA);
		}, 0);
	}

	void Hardware::processUcCycle()
	{
		syncUcToDSP();

		const auto deltaCycles = m_uc.exec();
		if(m_esaiFrameIndex > 0)
			m_remainingUcCycles -= static_cast<int64_t>(deltaCycles);

		for (auto& dsp : m_dsps)
			dsp->transferHostFlagsUc2Dsdp();

		for (auto& dsp : m_dsps)
			dsp->hdiTransferDSPtoUC();

		if(m_uc.requestDSPReset())
		{
			for (auto& dsp : m_dsps)
			{
				if(dsp->haveSentTXToDSP())
				{
//					m_uc.dumpMemory("DSPreset");
					assert(false && "DSP needs reset even though it got data already. Needs impl");
				}
			}
			m_uc.notifyDSPBooted();
		}
	}

	void Hardware::processAudio(uint32_t _frames, uint32_t _latency)
	{
		ensureBufferSize(_frames);

		if(m_esaiFrameIndex == 0)
			return;

		m_midi.process(_frames);

		m_processAudio = true;

		// DSP3 (index 1) outputs final audio on ESSI0, single DSP uses index 0
		auto& essiMain = m_dsps[getMainDspIdx()]->getPeriph().getEssi0();

		const dsp56k::TWord* inputs[16]{nullptr};
		dsp56k::TWord* outputs[16]{nullptr};

		inputs[0] = &m_audioInputs[0].front();
		inputs[1] = &m_audioInputs[1].front();
		inputs[2] = m_dummyInput.data();
		inputs[3] = m_dummyInput.data();
		inputs[4] = m_dummyInput.data();
		inputs[5] = m_dummyInput.data();
		inputs[6] = m_dummyInput.data();
		inputs[7] = m_dummyInput.data();

		outputs[0] = &m_audioOutputs[0].front();
		outputs[1] = &m_audioOutputs[1].front();
		outputs[2] = &m_audioOutputs[2].front();
		outputs[3] = &m_audioOutputs[3].front();
		outputs[4] = m_dummyOutput.data();
		outputs[5] = m_dummyOutput.data();
		outputs[6] = m_dummyOutput.data();
		outputs[7] = m_dummyOutput.data();
		outputs[8] = m_dummyOutput.data();
		outputs[9] = m_dummyOutput.data();
		outputs[10] = m_dummyOutput.data();
		outputs[11] = m_dummyOutput.data();

		while (_frames)
		{
			const auto processCount = std::min(_frames, static_cast<uint32_t>(1024));
			_frames -= processCount;

			if (m_useVoiceExpansion)
			{
				// One-shot DMA diagnostic after ~2 seconds of runtime
				static bool dmaDiagDone = false;
				if (!dmaDiagDone && m_esaiFrameIndex > 800000)
				{
					dmaDiagDone = true;
					for (uint32_t dspIdx = 0; dspIdx < getDspCount(); ++dspIdx)
					{
						auto& dma = m_dsps[dspIdx]->getPeriph().getDMA();
						for (uint32_t ch = 0; ch < 6; ++ch)
						{
							const auto dcr = dma.getDCR(ch);
							if (dcr == 0) continue;
							const auto de = (dcr >> 23) & 1;
							const auto tm = (dcr >> 19) & 7;
							const auto rs = (dcr >> 11) & 0x1f;
							LOG("DMA DIAG: DSP" << dspIdx << " DMA" << ch
								<< " DCR=" << HEX(dcr) << " DE=" << de << " TM=" << tm << " RS=" << rs
								<< " DSR=" << HEX(dma.getDSR(ch)) << " DDR=" << HEX(dma.getDDR(ch))
								<< " DCO=" << HEX(dma.getDCO(ch))
								<< " hasTrigger(Essi1TX)=" << dma.hasTrigger(dsp56k::DmaChannel::RequestSource::Essi1TransmitData));
						}
						auto& essi1 = m_dsps[dspIdx]->getPeriph().getEssi1();
						auto& essi0 = m_dsps[dspIdx]->getPeriph().getEssi0();
						LOG("DMA DIAG: DSP" << dspIdx
							<< " ESSI1 txOut=" << essi1.getAudioOutputs().size()
							<< " rxIn=" << essi1.getAudioInputs().size()
							<< " TE=" << essi1.hasEnabledTransmitters()
							<< " RE=" << essi1.hasEnabledReceivers()
							<< " CRA=" << HEX(static_cast<uint32_t>(essi1.getCRA()))
							<< " CRB=" << HEX(static_cast<uint32_t>(essi1.getCRB()))
							<< " SR=" << HEX(static_cast<uint32_t>(essi1.getSR()))
							<< " txWC=" << essi1.getTxWordCount()
							<< " | ESSI0 TE=" << essi0.hasEnabledTransmitters()
							<< " RE=" << essi0.hasEnabledReceivers());
					}
				}

				// ESSI1 ring routing is handled in real-time via TX callbacks (set in initVoiceExpansion)

				// Feed external audio input to DSP0 (exp1) via ESSI0 RX
				m_dsps[0]->getPeriph().getEssi0().processAudioInputInterleaved(inputs, processCount);

				// Feed silence to main DSP's ESSI0 RX (exp3 doesn't use external audio input)
				essiMain.processAudioInputInterleaved(inputs, processCount, _latency);
			}
			else
			{
				essiMain.processAudioInputInterleaved(inputs, processCount, _latency);
			}

			const auto requiredSize = processCount > 8 ? processCount - 8 : 0;

			if(essiMain.getAudioOutputs().size() < requiredSize)
			{
				std::unique_lock uLock(m_requestedFramesAvailableMutex);
				m_requestedFrames = requiredSize;
				m_requestedFramesAvailableCv.wait(uLock, [&]()
				{
					if(essiMain.getAudioOutputs().size() < requiredSize)
						return false;
					m_requestedFrames = 0;
					return true;
				});
			}

			essiMain.processAudioOutputInterleaved(outputs, processCount);

			if (m_useVoiceExpansion)
			{
				// Drain ESSI0 outputs from non-main DSPs to prevent buffer overflow
				for (uint32_t idx = 0; idx < 3; ++idx)
				{
					if (idx == getMainDspIdx())
						continue;
					auto& essi0 = m_dsps[idx]->getPeriph().getEssi0();
					dsp56k::TWord* dummyOuts[16]{nullptr};
					const auto available = essi0.getAudioOutputs().size();
					if (available >= 512)
						essi0.processAudioOutputInterleaved(dummyOuts, static_cast<uint32_t>(available >> 1));
				}
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

	void Hardware::ensureBufferSize(const uint32_t _frames)
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
