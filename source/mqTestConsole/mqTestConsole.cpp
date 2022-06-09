#include <iostream>
#include <memory>

#include "../mqLib/mqdsp.h"
#include "../synthLib/os.h"
#include "../synthLib/wavWriter.h"

#include "../mqLib/rom.h"
#include "../mqLib/mqmc.h"

#include "dsp56kEmu/dspthread.h"
#include "dsp56kEmu/interrupts.h"
#include "dsp56kEmu/interpreterunittests.h"

#include <vector>

#ifdef __APPLE_CC__
#include <stdio.h>
#include <termios.h>
#include <sys/ioctl.h>

int _kbhit() {
	static const int STDIN = 0; static bool init=false;
	if (!init) {termios term;tcgetattr(STDIN,&term);term.c_lflag&=~ICANON;tcsetattr(STDIN,TCSANOW,&term);setbuf(stdin,0);init=true;}
	int bytesWaiting;	ioctl(STDIN, FIONREAD, &bytesWaiting);	return bytesWaiting;
}
#define _getch() getchar()
#else
#include <conio.h>
#endif

namespace mqLib
{
	class MqDsp;
}

int main(int _argc, char* _argv[])
{
	try
	{
		dsp56k::InterpreterUnitTests tests;
	}
	catch(std::string& _err)
	{
		LOG("Unit Test failed: " << _err);
		return -1;
	}

	const auto romFile = synthLib::findROM(512 * 1024);

	if(romFile.empty())
	{
		std::cout << "Failed to find ROM, make sure that a ROM file with extension .bin is placed next to this executable" << std::endl;
		return -1;
	}

	mqLib::ROM m_rom(romFile);

	std::unique_ptr<mqLib::MqMc> m_uc;
	std::unique_ptr<mqLib::MqDsp> m_dsp;

	m_uc.reset(new mqLib::MqMc(m_rom));
	m_dsp.reset(new mqLib::MqDsp());

	std::deque<uint32_t> txData;

	auto& m_hdiUC = m_uc->hdi08();
	auto& m_hdiDSP = m_dsp->hdi08();
	auto& m_buttons = m_uc->getButtons();

	m_hdiDSP.setRXRateLimit(0);

	dsp56k::DSPThread m_dspThread(m_dsp->dsp());

	char ch = 0;
	std::thread inputReader([&ch, &m_dsp]
	{
		while(m_dsp.get())
		{
			if(_kbhit())
				ch = static_cast<char>(_getch());
		}
	});

	uint32_t m_hdiHF01 = 0;	// uc => DSP
	uint32_t m_hdiHF23 = 0;	// DSP => uc
	uint64_t m_dspCycles = 0;

	auto transferHostFlags = [&]()
	{
		// transfer HF 2&3 from uc to DSP
		auto hsr = m_hdiDSP.readStatusRegister();
		const auto prevHsr = hsr;
		hsr &= ~0x18;
		hsr |= (m_hdiHF01<<3);
		if(prevHsr != hsr)
			m_hdiDSP.writeStatusRegister(hsr);

		const auto hf23 = (m_hdiDSP.readControlRegister() >> 3) & 3;
		if(hf23 != m_hdiHF23)
		{
//			LOG("HDI HF23=" << HEXN(hf23,1));
			m_hdiHF23 = hf23;
		}
	};

	m_dsp->dsp().setExecCallback([&]()
	{
		transferHostFlags();
		if(m_hdiDSP.hasRXData())
			m_hdiDSP.exec();
	});

	std::array<std::vector<dsp56k::TWord>, 2> m_audioOutput;

	for (auto& audioOutput : m_audioOutput)
		audioOutput.resize(1024);

	bool silence = true;

	const std::string filename = "mq_output_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) + ".wav";
	synthLib::AsyncWriter wavWriter(filename, 44100);

	auto processAudio = [&]()
	{
		auto& esai = m_dsp->getPeriph().getEsai();
		const auto count = std::min(esai.getAudioOutputs().size()>>1, m_audioOutput[0].size()>>1);

		if(count >= 512)
		{
//			LOG("Drain ESAI");
			const dsp56k::TWord* dummyInputs[16]{nullptr};
			dsp56k::TWord* dummyOutputs[16]{nullptr};
			dummyOutputs[0] = &m_audioOutput[0].front();
			dummyOutputs[1] = &m_audioOutput[1].front();
			esai.processAudioInterleaved(dummyInputs, dummyOutputs, static_cast<uint32_t>(count));

			for(int i=static_cast<int>(count) - 1; i>=0; --i)
			{
				m_audioOutput[0][(i<<1)    ] = m_audioOutput[0][i];
				m_audioOutput[0][(i<<1) + 1] = m_audioOutput[1][i];

				if(silence && (m_audioOutput[0][i] || m_audioOutput[1][i]))
					silence = false;
			}

			if(!silence)
			{
				wavWriter.append([&](auto& _dst)
				{
					_dst.reserve(_dst.size() + count * 2);
					for(size_t i=0; i<count*2; ++i)
						_dst.push_back(m_audioOutput[0][i]);
				}
				);
//				wavWriter.write(filename, 32, true, 2, 44100, &m_audioOutput[0].front(), sizeof(float) * count * 2);
			}
		}
	};

	uint32_t m_dspInstructionCounter = 0;
	bool m_requestNMI = false;
	auto m_haveSentTXtoDSP = false;
	
	m_hdiDSP.setTransmitDataAlwaysEmpty(false);

	auto injectUCtoDSPInterrupts = [&]()
	{
		if(m_requestNMI && !m_uc->requestDSPinjectNMI())
		{
			LOG("uc request DSP NMI");
			m_dsp->dsp().injectInterrupt(dsp56k::Vba_NMI);
		}
		m_requestNMI = m_uc->requestDSPinjectNMI();

		uint8_t interruptAddr;
		bool injected = false;
		while(m_hdiUC.pollInterruptRequest(interruptAddr))
		{
//			LOG("Inject interrupt " << HEXN(interruptAddr, 2));
			injected = true;
			if(!m_dsp->dsp().injectInterrupt(interruptAddr))
				LOG("Interrupt request FAILED, interrupt was masked");
		}

		while(m_dsp->dsp().hasPendingInterrupts())
		{
			processAudio();
			std::this_thread::yield();
		}
//		if(injected)
//			LOG("No interrupts pending");
	};

	auto hdiTransferDSPtoUC = [&]()
	{
		if(m_hdiDSP.hasTX() && m_hdiUC.canReceiveData())
		{
			const auto v = m_hdiDSP.readTX();
//			LOG("HDI uc2dsp=" << HEX(v));
			m_hdiUC.writeRx(v);
			return true;
		}
		return false;
	};

	auto hdiTransferUCtoDSP = [&]()
	{
		m_hdiUC.pollTx(txData);

		if(txData.empty())
			return;

		for (const uint32_t data : txData)
		{
			m_haveSentTXtoDSP = true;
//			LOG("toDSP writeRX=" << HEX(data));
			m_hdiDSP.writeRX(&data, 1);
		}
		txData.clear();
		while((m_hdiDSP.hasRXData() && m_hdiDSP.rxInterruptEnabled()) || m_dsp->dsp().hasPendingInterrupts())
		{
			processAudio();
			std::this_thread::yield();
		}
//		LOG("writeRX wait over");
	};

	m_hdiUC.setRxEmptyCallback([&](bool needMoreData)
	{
		injectUCtoDSPInterrupts();

		m_hdiDSP.injectTXInterrupt();

		if(needMoreData)
		{
			while(!m_hdiDSP.hasTX() && m_hdiDSP.txInterruptEnabled())
			{
				processAudio();
				std::this_thread::yield();
			}
		}

		if(!hdiTransferDSPtoUC())
		{
			int foo=0;
		}
	});

	uint32_t m_esaiFrameIndex = 0;
	uint32_t m_lastEsaiFrameIndex = 0;
	int32_t m_remainingMcCycles = 0;

	m_dsp->getPeriph().getEsai().setCallback([&](dsp56k::Audio*)
	{
		++m_esaiFrameIndex;
	}, 0);

	while(m_dsp.get())
	{
		const auto instructionCounter = m_dsp->dsp().getInstructionCounter();
		const auto d = dsp56k::delta(instructionCounter, m_dspInstructionCounter);
		m_dspInstructionCounter = instructionCounter;
		m_dspCycles += d;

		// we can only use ESAI once it has been enabled
		if(m_esaiFrameIndex > m_lastEsaiFrameIndex)
		{
			const auto mcClock = m_uc->getSim().getSystemClockHz();
			const auto mcCyclesPerFrame = mcClock / (44100 * 2);	// stereo interleaved

			m_remainingMcCycles += mcCyclesPerFrame * (m_esaiFrameIndex - m_lastEsaiFrameIndex);
			m_lastEsaiFrameIndex = m_esaiFrameIndex;
		}
		if(m_esaiFrameIndex > 0)
		{
			if(m_remainingMcCycles < 0)
			{
				processAudio();
				std::this_thread::yield();
				continue;
			}
		}
		/*else if(mc->getCycles() > dspCycles/5)
		{
			processAudio();
			std::this_thread::yield();
			continue;
		}*/

		const auto deltaCycles = m_uc->exec();
		if(m_esaiFrameIndex > 0)
			m_remainingMcCycles -= deltaCycles;

		if(ch)
		{
			switch (ch)
			{
			case '1':				m_buttons.toggleButton(mqLib::Buttons::ButtonType::Inst1);				break;
			case '2':				m_buttons.toggleButton(mqLib::Buttons::ButtonType::Inst2);				break;
			case '3':				m_buttons.toggleButton(mqLib::Buttons::ButtonType::Inst3);				break;
			case '4':				m_buttons.toggleButton(mqLib::Buttons::ButtonType::Inst4);				break;
			case '5':				m_buttons.toggleButton(mqLib::Buttons::ButtonType::Down);				break;
			case '6':				m_buttons.toggleButton(mqLib::Buttons::ButtonType::Left);				break;
			case '7':				m_buttons.toggleButton(mqLib::Buttons::ButtonType::Right);				break;
			case '8':				m_buttons.toggleButton(mqLib::Buttons::ButtonType::Up);					break;
			case 'q':				m_buttons.toggleButton(mqLib::Buttons::ButtonType::Global);				break;
			case 'w':				m_buttons.toggleButton(mqLib::Buttons::ButtonType::Multi);				break;
			case 'e':				m_buttons.toggleButton(mqLib::Buttons::ButtonType::Edit);				break;
			case 'r':				m_buttons.toggleButton(mqLib::Buttons::ButtonType::Sound);				break;
			case 't':				m_buttons.toggleButton(mqLib::Buttons::ButtonType::Shift);				break;
			case 'y':				m_buttons.toggleButton(mqLib::Buttons::ButtonType::Power);				break;
			case 'z':				m_buttons.toggleButton(mqLib::Buttons::ButtonType::Multimode);			break;
			case 'u':				m_buttons.toggleButton(mqLib::Buttons::ButtonType::Peek);				break;
			case 'i':				m_buttons.toggleButton(mqLib::Buttons::ButtonType::Play);				break;
			case 's':				m_buttons.rotate(mqLib::Buttons::Encoders::LcdLeft, 1);					break;
			case 'x':				m_buttons.rotate(mqLib::Buttons::Encoders::LcdLeft, -1);				break;
			case 'd':				m_buttons.rotate(mqLib::Buttons::Encoders::LcdRight, 1);				break;
			case 'c':				m_buttons.rotate(mqLib::Buttons::Encoders::LcdRight, -1);				break;
			case 'f':				m_buttons.rotate(mqLib::Buttons::Encoders::Master, 1);					break;
			case 'v':				m_buttons.rotate(mqLib::Buttons::Encoders::Master, -1);					break;
			case 'g':				m_buttons.rotate(mqLib::Buttons::Encoders::Matrix1, 1);					break;
			case 'b':				m_buttons.rotate(mqLib::Buttons::Encoders::Matrix1, -1);				break;
			case 'h':				m_buttons.rotate(mqLib::Buttons::Encoders::Matrix2, 1);					break;
			case 'n':				m_buttons.rotate(mqLib::Buttons::Encoders::Matrix2, -1);				break;
			case 'j':				m_buttons.rotate(mqLib::Buttons::Encoders::Matrix3, 1);					break;
			case 'm':				m_buttons.rotate(mqLib::Buttons::Encoders::Matrix3, -1);				break;
			case 'k':				m_buttons.rotate(mqLib::Buttons::Encoders::Matrix4, 1);					break;
			case ',':				m_buttons.rotate(mqLib::Buttons::Encoders::Matrix4, -1);				break;
			case '9':
				// Midi Note On
				m_uc->getQSM().writeSciRX(0x90);
				m_uc->getQSM().writeSciRX(60);
				m_uc->getQSM().writeSciRX(0x7f);
				break;
			case '0':	
				// Midi Note Off
				m_uc->getQSM().writeSciRX(0x80);
				m_uc->getQSM().writeSciRX(60);
				m_uc->getQSM().writeSciRX(0x7f);
				break;
			case 'o':
				// Modwheel Max
				m_uc->getQSM().writeSciRX(0xb0);
				m_uc->getQSM().writeSciRX(1);
				m_uc->getQSM().writeSciRX(0x7f);
				break;
			case 'p':	
				// Modwheel Min
				m_uc->getQSM().writeSciRX(0xb0);
				m_uc->getQSM().writeSciRX(1);
				m_uc->getQSM().writeSciRX(0x0);
				break;
			case '!':
				m_dsp->dumpPMem("dsp_dump_P_" + std::to_string(m_dspCycles));
				break;
			case '"':
				m_uc->dumpMemory("mc_dump_mem");
				break;
			case '$':
				m_uc->dumpROM("rom_runtime");
				break;
			case '%':
				{
					constexpr uint8_t testPatch[] =	
					{
						0xf0,0x3e,0x10,0x00,0x10,0x00,0x00,0x01,0x40,0x40,0x40,0x42,0x60,0x00,0x00,0x01,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x40,0x40,0x40,0x42,0x60,0x00,0x00,0x01,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x40,0x40,0x40,0x42,0x60,0x00,0x00,0x01,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x7f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x64,0x40,0x64,0x40,0x64,0x40,0x00,0x40,0x40,0x40,0x00,0x40,0x00,0x00,0x00,0x00,0x01,0x7f,0x40,0x00,0x00,0x40,0x40,0x40,0x40,0x60,0x40,0x40,0x03,0x40,0x00,0x00,0x40,0x00,0x40,0x00,0x01,0x7f,0x40,0x00,0x00,0x40,0x40,0x40,0x40,0x60,0x40,0x40,0x00,0x40,0x00,0x00,0x40,0x00,0x40,0x00,0x00,0x01,0x00,0x00,0x64,0x40,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x00,0x40,0x01,0x00,0x40,0x00,0x00,0x7f,0x40,0x7f,0x00,0x00,0x0a,0x00,0x00,0x00,0x40,0x00,0x00,0x7f,0x40,0x7f,0x00,0x00,0x0a,0x00,0x00,0x00,0x40,0x00,0x00,0x7f,0x40,0x7f,0x00,0x00,0x0a,0x00,0x00,0x00,0x40,0x00,0x00,0x7f,0x40,0x7f,0x00,0x00,0x0a,0x00,0x00,0x01,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x40,0x00,0x00,0x40,0x00,0x00,0x40,0x00,0x00,0x40,0x00,0x00,0x40,0x00,0x00,0x40,0x00,0x00,0x40,0x00,0x00,0x40,0x00,0x00,0x40,0x00,0x00,0x40,0x00,0x00,0x40,0x00,0x00,0x40,0x00,0x00,0x40,0x00,0x00,0x40,0x00,0x00,0x40,0x00,0x00,0x40,0x00,0x00,0x40,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x37,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x00,0x00,0x00,0x4c,0x66,0x6f,0x32,0x4f,0x73,0x63,0x50,0x69,0x74,0x63,0x68,0x20,0x20,0x20,0x20,0x49,0x4e,0x49,0x54,0x00,0xf7
					};

					for (uint8_t byte : testPatch)
						m_uc->getQSM().writeSciRX(byte);
				}
				break;
			default:																						break;
			}
			ch = 0;
		}

		const uint32_t hf01 = (m_hdiUC.icr() >> 3) & 3;

		if(hf01 != m_hdiHF01)
		{
//			LOG("HDI HF01=" << HEXN(hf01,1));
			m_hdiHF01 = hf01;
		}

		injectUCtoDSPInterrupts();

		// transfer DSP host flags HF2&3 to uc
		auto isr = m_hdiUC.isr();
		const auto prevIsr = isr;
		isr &= ~0x18;
		isr |= (m_hdiHF23<<3);
		if(isr != prevIsr)
			m_hdiUC.isr(isr);

		hdiTransferUCtoDSP();
		hdiTransferDSPtoUC();

		if(m_uc->requestDSPReset())
		{
			if(m_haveSentTXtoDSP)
				m_uc->dumpMemory("DSPreset");
			assert(!haveSentTXtoDSP && "DSP needs reset even though it got data already. Needs impl");
			m_uc->notifyDSPBooted();
		}

		processAudio();
	}

	return 0;
}
