#include "mqhardware.h"

#include "synthLib/midiBufferParser.h"
#include "synthLib/deviceException.h"

#include <cstring>	// memcpy

namespace mqLib
{
	Hardware::Hardware(const ROM& _rom)
		: wLib::Hardware(44100)
		, m_rom(_rom)
		, m_uc(m_rom)
#if MQ_VOICE_EXPANSION
		, m_dsps{MqDsp(*this, m_uc.getHdi08A().getHdi08(), 0), MqDsp(*this, m_uc.getHdi08B().getHdi08(), 1) , MqDsp(*this, m_uc.getHdi08C().getHdi08(), 2)}
#else
		, m_dsps{MqDsp(*this, m_uc.getHdi08A().getHdi08(), 0)}
#endif
		, m_midi(m_uc.getQSM(), 44100)
	{
		if(!m_rom.isValid())
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing);

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
		/*
		m_dsps[1].getPeriph().getPortC().hostWrite(0x10);	// set bit 4 of GPIO Port C, vexp DSPs are waiting for this
		m_dsps[2].getPeriph().getPortC().hostWrite(0x10);	// set bit 4 of GPIO Port C, vexp DSPs are waiting for this

		bool done = false;

		auto& esaiA = m_dsps[0].getPeriph().getEsai();
		auto& esaiB = m_dsps[1].getPeriph().getEsai();
		auto& esaiC = m_dsps[2].getPeriph().getEsai();

		esaiA.setCallback([&](dsp56k::Audio*)
		{
		}, 0);

		esaiB.setCallback([&](dsp56k::Audio*)
		{
		}, 0);

		esaiC.setCallback([&](dsp56k::Audio*)
		{
		}, 0);

		while (!m_dsps.front().receivedMagicEsaiPacket())
		{
			// vexp1 only needs the audio input
			esaiB.getAudioInputs().push_back({0});

			// transfer output from vexp1 to vexp2
			auto out = esaiB.getAudioOutputs().pop_front();
			std::array<dsp56k::TWord, 4> in{ out[0], out[1], out[2], 0};
			esaiC.getAudioInputs().push_back(in);

			// read output of vexp2 and send to main
			out = esaiC.getAudioOutputs().pop_front();

			// this should consist of RX0 = audio input, RX1/2 = vexp2 output TX1/TX2
			in = {0, out[1], out[2]};
			esaiA.getAudioInputs().push_back(in);

			// final output 0,1,2 = audio outs
			out = esaiA.getAudioOutputs().pop_front();
		}
		LOG("Voice Expansion initialization completed");
		setupEsaiListener();
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
					m_uc.dumpMemory("DSPreset");
					assert(false && "DSP needs reset even though it got data already. Needs impl");
				}
			}
			m_uc.notifyDSPBooted();
		}
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

			if constexpr (g_useVoiceExpansion)
			{
				auto& esaiA = m_dsps[0].getPeriph().getEsai();
				auto& esaiB = m_dsps[1].getPeriph().getEsai();
				auto& esaiC = m_dsps[2].getPeriph().getEsai();
				/*
				const auto tccrA = esaiA.getTccrAsString();	const auto rccrA = esaiA.getRccrAsString();
				const auto tcrA = esaiA.getTcrAsString();	const auto rcrA = esaiA.getRcrAsString();

				const auto tccrB = esaiB.getTccrAsString();	const auto rccrB = esaiB.getRccrAsString();
				const auto tcrB = esaiB.getTcrAsString();	const auto rcrB = esaiB.getRcrAsString();

				const auto tccrC = esaiC.getTccrAsString();	const auto rccrC = esaiC.getRccrAsString();
				const auto tcrC = esaiC.getTcrAsString();	const auto rcrC = esaiC.getRcrAsString();

				LOG("ESAI DSPmain:\n" << tccrA << '\n' << tcrA << '\n' << rccrA << '\n' << rcrA << '\n');
				LOG("ESAI VexpA:\n"   << tccrB << '\n' << tcrB << '\n' << rccrB << '\n' << rcrB << '\n');
				LOG("ESAI VexpB:\n"   << tccrC << '\n' << tcrC << '\n' << rccrC << '\n' << rcrC << '\n');
				*/
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
			{
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
