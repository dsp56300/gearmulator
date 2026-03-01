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

	Hardware::Hardware(const std::vector<uint8_t>& _romData, const std::string& _romName)
		: wLib::Hardware(40000)
		, m_rom(initializeRom(_romData, _romName))
		, m_uc(m_rom)
#if XT_VOICE_EXPANSION
		, m_dsps{DSP(*this, m_uc.getHdi08B().getHdi08(), 0), DSP(*this, m_uc.getHdi08C().getHdi08(), 1), DSP(*this, m_uc.getHdi08A().getHdi08(), 2)}
#else
		, m_dsps{DSP(*this, m_uc.getHdi08A().getHdi08(), 0)}
#endif
		, m_midi(m_uc)
	{
		if(!m_rom.isValid())
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing);
	}

	Hardware::~Hardware()
	{
		m_dsps[g_mainDspIdx].getPeriph().getEssi0().setCallback({}, 0);
	}

	void Hardware::process()
	{
		processUcCycle();
	}
	
	void Hardware::resetMidiCounter()
	{
		// wait for DSP to enter blocking state
		const auto& esai = m_dsps[g_mainDspIdx].getPeriph().getEssi0();

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

		// During boot, set empty callbacks on all ESSI interfaces to prevent DSP threads from blocking
		for (uint32_t i = 0; i < 3; ++i)
		{
			m_dsps[i].getPeriph().getEssi0().setCallback([](dsp56k::Audio*) {}, 0);
			m_dsps[i].getPeriph().getEssi1().setCallback([](dsp56k::Audio*) {}, 0);
		}

		// Boot pump loop: DSPs sync via ESSI1 ring (DSP2 TX→DSP0 RX→DSP0 TX→DSP1 RX→DSP1 TX→DSP2 RX).
		// DSP2 (exp3, idx 2) sends magic $535400 on ESSI1 TX, DSP0/DSP1 echo it forward.
		// We must route ESSI1 data between DSPs for the sync handshake to complete.
		auto& mainEssi0 = m_dsps[g_mainDspIdx].getPeriph().getEssi0();

		// ESSI1 ring connections: [from].TX → [to].RX
		constexpr uint32_t essi1From[] = {2, 0, 1};  // TX source DSP index
		constexpr uint32_t essi1To[]   = {0, 1, 2};  // RX destination DSP index

		while (mainEssi0.getAudioOutputs().empty())
		{
			// Route ESSI1 data around the ring
			for (int leg = 0; leg < 3; ++leg)
			{
				auto& txOut = m_dsps[essi1From[leg]].getPeriph().getEssi1().getAudioOutputs();
				auto& rxIn  = m_dsps[essi1To[leg]].getPeriph().getEssi1().getAudioInputs();

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
				auto& in0 = m_dsps[i].getPeriph().getEssi0().getAudioInputs();
				if (in0.empty())
					in0.push_back({});
			}

			// Drain ESSI0 outputs from non-main DSPs to prevent overflow
			for (uint32_t i = 0; i < 3; ++i)
			{
				if (i != g_mainDspIdx)
				{
					auto& out0 = m_dsps[i].getPeriph().getEssi0().getAudioOutputs();
					while (out0.size() > 32)
						out0.pop_front();
				}
			}

			std::this_thread::yield();
		}

		LOG("Voice Expansion boot completed, main DSP index " << g_mainDspIdx);

		// Set real audio callback on main DSP's ESSI0
		for (auto& dsp : m_dsps)
		{
			dsp.getPeriph().getEssi0().writeEmptyAudioIn(8192);
			dsp.getPeriph().getEssi1().writeEmptyAudioIn(8192);
		}
		mainEssi0.setCallback([this, &mainEssi0](dsp56k::Audio*)
		{
			m_bootCompleted = true;
			onEsaiCallback(mainEssi0);
		}, 0);
	}

	void Hardware::setupEsaiListener()
	{
		auto& esaiA = m_dsps.front().getPeriph().getEssi0();

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
			dsp.transferHostFlagsUc2Dsdp();

		for (auto& dsp : m_dsps)
			dsp.hdiTransferDSPtoUC();

		if(m_uc.requestDSPReset())
		{
			for (auto& dsp : m_dsps)
			{
				if(dsp.haveSentTXToDSP())
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
		auto& essiMain = m_dsps[g_mainDspIdx].getPeriph().getEssi0();

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

			if constexpr (g_useVoiceExpansion)
			{
				// ESSI1 ring routing: DSP2(idx 2) TX→DSP0(idx 0) RX→DSP0 TX→DSP1(idx 1) RX→DSP1 TX→DSP2 RX
				constexpr uint32_t essi1From[] = {2, 0, 1};
				constexpr uint32_t essi1To[]   = {0, 1, 2};

				for (int leg = 0; leg < 3; ++leg)
				{
					auto& txOut = m_dsps[essi1From[leg]].getPeriph().getEssi1().getAudioOutputs();
					auto& rxIn  = m_dsps[essi1To[leg]].getPeriph().getEssi1().getAudioInputs();

					while (!txOut.empty() && !rxIn.full())
					{
						dsp56k::Audio::RxFrame rx;
						txToRx(txOut.pop_front(), rx);
						rxIn.push_back({std::move(rx)});
					}
				}

				// Feed external audio input to DSP0 (exp1) via ESSI0 RX
				m_dsps[0].getPeriph().getEssi0().processAudioInputInterleaved(inputs, processCount);

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

			if constexpr (g_useVoiceExpansion)
			{
				// Drain ESSI0 outputs from non-main DSPs to prevent buffer overflow
				for (uint32_t idx = 0; idx < 3; ++idx)
				{
					if (idx == g_mainDspIdx)
						continue;
					auto& essi0 = m_dsps[idx].getPeriph().getEssi0();
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
