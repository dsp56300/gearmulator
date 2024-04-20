#include "xtHardware.h"

#include "../synthLib/midiTypes.h"
#include "../synthLib/midiBufferParser.h"
#include "../synthLib/deviceException.h"

#include <cstring>	// memcpy

namespace xt
{
	constexpr uint32_t g_syncEsaiFrameRate = 8;
	constexpr uint32_t g_syncHaltDspEsaiThreshold = 16;

	static_assert((g_syncEsaiFrameRate & (g_syncEsaiFrameRate - 1)) == 0, "esai frame sync rate must be power of two");
	static_assert(g_syncHaltDspEsaiThreshold >= g_syncEsaiFrameRate * 2, "esai DSP halt threshold must be greater than two times the sync rate");

	Hardware::Hardware(std::string _romFilename)
		: m_romFileName(std::move(_romFilename))
		, m_rom(m_romFileName, nullptr)
		, m_uc(m_rom)
		, m_dsps{DSP(*this, m_uc.getHdi08A().getHdi08(), 0)}
		, m_midi(m_uc.getQSM())
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
	
	void Hardware::sendMidi(const synthLib::SMidiEvent& _ev)
	{
		m_midiIn.push_back(_ev);
	}

	void Hardware::receiveMidi(std::vector<uint8_t>& _data)
	{
		m_midi.readTransmitBuffer(_data);
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
/*			auto& dsp = m_dsps.front().dsp();
			auto& mem = dsp.memory();
			mem.saveAssembly("xt_dspA.asm", 0, mem.sizeP(), true, false, dsp.getPeriph(0), dsp.getPeriph(1));
*/
			m_bootCompleted = true;
			++m_esaiFrameIndex;

			processMidiInput();

			if((m_esaiFrameIndex & (g_syncEsaiFrameRate-1)) == 0)
				m_esaiFrameAddedCv.notify_one();

			m_requestedFramesAvailableMutex.lock();

			if(m_requestedFrames && esaiA.getAudioOutputs().size() >= m_requestedFrames)
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
	}

	void Hardware::processUcCycle()
	{
		syncUcToDSP();

		const auto deltaCycles = m_uc.exec();
		if(m_esaiFrameIndex > 0)
			m_remainingUcCycles -= static_cast<int64_t>(deltaCycles);

		for (auto& dsp : m_dsps)
			dsp.transferHostFlagsUc2Dsdp();

		hdiProcessUCtoDSPNMIIrq();

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

		constexpr double divInv = 1.0 / 40000.0;
		const double ucCyclesPerFrame = static_cast<double>(ucClock) * divInv;

		const auto esaiDelta = esaiFrameIndex - m_lastEsaiFrameIndex;

		m_remainingUcCyclesD += ucCyclesPerFrame * static_cast<double>(esaiDelta);
		m_remainingUcCycles += static_cast<int64_t>(m_remainingUcCyclesD);
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

	void Hardware::processMidiInput()
	{
		++m_midiOffsetCounter;

		while(!m_midiIn.empty())
		{
			const auto& e = m_midiIn.front();

			if(e.offset > m_midiOffsetCounter)
				break;

			if(!e.sysex.empty())
			{
				m_midi.writeMidi(e.sysex);
			}
			else
			{
				m_midi.writeMidi(e.a);
				const auto len = synthLib::MidiBufferParser::lengthFromStatusByte(e.a);
				if (len > 1)
					m_midi.writeMidi(e.b);
				if (len > 2)
					m_midi.writeMidi(e.c);
			}

			m_midiIn.pop_front();
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

		// TODO: Right audio input channel needs to be delayed by one frame
		::memcpy(&m_delayedAudioIn[1], m_audioInputs[1].data(), sizeof(dsp56k::TWord) * _frames);

		inputs[1] = &m_audioInputs[0].front();
		inputs[0] = m_delayedAudioIn.data();
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
		outputs[5] = m_dummyOutput.data();
		outputs[4] = m_dummyOutput.data();
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

		m_delayedAudioIn[0] = m_audioInputs[1][totalFrames-1];

		m_processAudio = false;
	}

	void Hardware::ensureBufferSize(const uint32_t _frames)
	{
		if(m_audioInputs.front().size() < _frames)
		{
			for (auto& input : m_audioInputs)
				input.resize(_frames);
		}

		if(m_delayedAudioIn.size() < _frames + 1)
			m_delayedAudioIn.resize(_frames + 1);

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
