#include "xtHardware.h"

#include "synthLib/midiBufferParser.h"
#include "synthLib/deviceException.h"

#include <cstring>	// memcpy

#include "xtRomLoader.h"

namespace xt
{
	Hardware::Hardware()
		: wLib::Hardware(40000)
		, m_rom(RomLoader::findROM())
		, m_uc(m_rom)
		, m_dsps{DSP(*this, m_uc.getHdi08A().getHdi08(), 0)}
		, m_midi(m_uc.getQSM(), 40000)
	{
		if(!m_rom.isValid())
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing);
	}

	Hardware::~Hardware()
	{
		m_dsps.front().getPeriph().getEssi0().setCallback({}, 0);
	}

	void Hardware::process()
	{
		processUcCycle();
	}
	
	void Hardware::resetMidiCounter()
	{
		// wait for DSP to enter blocking state

		const auto& esai = m_dsps.front().getPeriph().getEssi0();

		auto& inputs = esai.getAudioInputs();
		auto& outputs = esai.getAudioOutputs();

		while(inputs.size() > 2 && !outputs.full())
			std::this_thread::yield();

		m_midiOffsetCounter = 0;
	}

	void Hardware::initVoiceExpansion()
	{
		if (m_dsps.size() < 3)
		{
			setupEsaiListener();
			return;
		}
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

		auto& esai = m_dsps.front().getPeriph().getEssi0();

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

		const auto totalFrames = _frames;

		while (_frames)
		{
			const auto processCount = std::min(_frames, static_cast<uint32_t>(1024));
			_frames -= processCount;
/*
			if constexpr (g_useVoiceExpansion)
			{
				auto& esaiA = m_dsps[0].getPeriph().getEsai();
				auto& esaiB = m_dsps[1].getPeriph().getEsai();
				auto& esaiC = m_dsps[2].getPeriph().getEsai();
				
				const auto tccrA = esaiA.getTccrAsString();	const auto rccrA = esaiA.getRccrAsString();
				const auto tcrA = esaiA.getTcrAsString();	const auto rcrA = esaiA.getRcrAsString();

				const auto tccrB = esaiB.getTccrAsString();	const auto rccrB = esaiB.getRccrAsString();
				const auto tcrB = esaiB.getTcrAsString();	const auto rcrB = esaiB.getRcrAsString();

				const auto tccrC = esaiC.getTccrAsString();	const auto rccrC = esaiC.getRccrAsString();
				const auto tcrC = esaiC.getTcrAsString();	const auto rcrC = esaiC.getRcrAsString();

				LOG("ESAI DSPmain:\n" << tccrA << '\n' << tcrA << '\n' << rccrA << '\n' << rcrA << '\n');
				LOG("ESAI VexpA:\n"   << tccrB << '\n' << tcrB << '\n' << rccrB << '\n' << rcrB << '\n');
				LOG("ESAI VexpB:\n"   << tccrC << '\n' << tcrC << '\n' << rccrC << '\n' << rcrC << '\n');

				// vexp1 only needs the audio input
				esaiB.processAudioInputInterleaved(inputs, processCount);

				// transfer output from vexp1 to vexp2
				esaiB.processAudioOutputInterleaved(outputs, processCount);

				const dsp56k::TWord* in[] = { outputs[0], outputs[1], outputs[2], outputs[3], outputs[4], outputs[5], nullptr, nullptr };
				esaiC.processAudioInputInterleaved(in, processCount);

				// read output of vexp2 and send to main
				esaiC.processAudioOutputInterleaved(outputs, processCount);

				// RX1/2 = vexp2 output TX1/TX2
				const dsp56k::TWord* inA[] = { inputs[1], inputs[0], outputs[2], outputs[3], outputs[4], outputs[5], nullptr, nullptr };
				esaiA.processAudioInputInterleaved(inA, processCount);

				// final output 0,1,2 = audio outs below
			}
			else
*/			{
				esai.processAudioInputInterleaved(inputs, processCount, _latency);
			}

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
			/*
			if constexpr (g_useVoiceExpansion)
			{
				for (uint32_t i = 1; i < 3; ++i)
				{
					auto& e = m_dsps[i].getPeriph().getEsai();

					dsp56k::TWord* outs[16]{ nullptr };
					if (e.getAudioOutputs().size() >= 512)
						e.processAudioOutputInterleaved(outs, static_cast<uint32_t>(e.getAudioOutputs().size() >> 1));
				}
			}
			*/
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
