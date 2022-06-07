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

	const auto romFile = synthLib::findROM(524288);

	mqLib::ROM rom(romFile);

	std::unique_ptr<mqLib::MqMc> mc;
	std::unique_ptr<mqLib::MqDsp> dsp;

	mc.reset(new mqLib::MqMc(rom));
	dsp.reset(new mqLib::MqDsp());

	std::deque<uint32_t> txData;

	auto& hdiUC = mc->hdi08();
	auto& hdiDSP = dsp->hdi08();
	auto& buttons = mc->getButtons();

	hdiDSP.setRXRateLimit(0);

	dsp56k::DSPThread dspThread(dsp->dsp());
	bool dumpDSP = false;

	char ch = 0;
	std::thread inputReader([&ch, &dsp]
	{
		while(dsp.get())
		{
			if(_kbhit())
				ch = static_cast<char>(_getch());
		}
	});

	uint32_t hdiHF01 = 0;	// uc => DSP
	uint32_t hdiHF23 = 0;	// DSP => uc
	uint64_t dspCycles = 0;

	auto transferHostFlags = [&]()
	{
		// transfer HF 2&3 from uc to DSP
		auto hsr = hdiDSP.readStatusRegister();
		const auto prevHsr = hsr;
		hsr &= ~0x18;
		hsr |= (hdiHF01<<3);
		if(prevHsr != hsr)
			hdiDSP.writeStatusRegister(hsr);

		const auto hf23 = (hdiDSP.readControlRegister() >> 3) & 3;
		if(hf23 != hdiHF23)
		{
//			LOG("HDI HF23=" << HEXN(hf23,1));
			hdiHF23 = hf23;
		}
	};

	dsp->dsp().setExecCallback([&]()
	{
		transferHostFlags();
		if(hdiDSP.hasRXData())
			hdiDSP.exec();
	});

	std::array<std::vector<float>, 2> m_audioOutput;

	for (auto& audioOutput : m_audioOutput)
		audioOutput.resize(1024);

	bool silence = true;

	synthLib::WavWriter wavWriter;
	const std::string filename = "mq_output_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) + ".wav";
	auto processAudio = [&]()
	{
		auto& esai = dsp->getPeriph().getEsai();
		const auto count = std::min(esai.getAudioOutputs().size()>>1, m_audioOutput[0].size()>>1);

		if(count >= 512)
		{
//			LOG("Drain ESAI");
			const float* dummyInputs[16]{nullptr};
			float* dummyOutputs[16]{nullptr};
			dummyOutputs[0] = &m_audioOutput[0].front();
			dummyOutputs[1] = &m_audioOutput[1].front();
			esai.processAudioInterleaved(dummyInputs, dummyOutputs, static_cast<uint32_t>(count));

			for(int i=static_cast<int>(count) - 1; i>=0; --i)
			{
				m_audioOutput[0][(i<<1)    ] = m_audioOutput[0][i];
				m_audioOutput[0][(i<<1) + 1] = m_audioOutput[1][i];

				if(silence && (m_audioOutput[0][i] != 0.0f || m_audioOutput[1][i] != 0.0f))
					silence = false;
			}

			if(!silence)
				wavWriter.write(filename, 32, true, 2, 44100, &m_audioOutput[0].front(), sizeof(float) * count * 2);
		}
	};

	uint32_t prevInstructions = 0;
	bool requestNMI = false;
	auto haveSentTXtoDSP = false;
	
	hdiDSP.setTransmitDataAlwaysEmpty(false);

	auto injectUCtoDSPInterrupts = [&]()
	{
		if(requestNMI && !mc->requestDSPinjectNMI())
		{
			LOG("uc request DSP NMI");
			dsp->dsp().injectInterrupt(dsp56k::Vba_NMI);
		}
		requestNMI = mc->requestDSPinjectNMI();

		uint8_t interruptAddr;
		bool injected = false;
		while(hdiUC.pollInterruptRequest(interruptAddr))
		{
//			LOG("Inject interrupt " << HEXN(interruptAddr, 2));
			injected = true;
			if(!dsp->dsp().injectInterrupt(interruptAddr))
				LOG("Interrupt request FAILED, interrupt was masked");
		}

		while(dsp->dsp().hasPendingInterrupts())
		{
			processAudio();
			std::this_thread::yield();
		}
//		if(injected)
//			LOG("No interrupts pending");
	};

	auto hdiTransferDSPtoUC = [&]()
	{
		if(hdiDSP.hasTX() && hdiUC.canReceiveData())
		{
			hdiUC.writeRx(hdiDSP.readTX());
			return true;
		}
		return false;
	};

	auto hdiTransferUCtoDSP = [&]()
	{
		hdiUC.pollTx(txData);

		if(txData.empty())
			return;

		for (const uint32_t data : txData)
		{
			haveSentTXtoDSP = true;
//			LOG("toDSP writeRX=" << HEX(data));
			hdiDSP.writeRX(&data, 1);
		}
		txData.clear();
		while((hdiDSP.hasRXData() && hdiDSP.rxInterruptEnabled()) || dsp->dsp().hasPendingInterrupts())
		{
			processAudio();
			std::this_thread::yield();
		}
//		LOG("writeRX wait over");
	};

	hdiUC.setRxEmptyCallback([&](bool needMoreData)
	{
		injectUCtoDSPInterrupts();

		hdiDSP.injectTXInterrupt();

		if(needMoreData)
		{
			while(!hdiDSP.hasTX() && hdiDSP.txInterruptEnabled())
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
	
	while(dsp.get())
	{
		const auto instructionCounter = dsp->dsp().getInstructionCounter();
		const auto d = dsp56k::delta(instructionCounter, prevInstructions);
		prevInstructions = instructionCounter;
		dspCycles += d;

		if(true && mc->getCycles() > dspCycles/6)
		{
			processAudio();
			std::this_thread::yield();
			continue;
		}

		mc->exec();

		if(ch)
		{
			switch (ch)
			{
			case '1':				buttons.toggleButton(mqLib::Buttons::ButtonType::Inst1);				break;
			case '2':				buttons.toggleButton(mqLib::Buttons::ButtonType::Inst2);				break;
			case '3':				buttons.toggleButton(mqLib::Buttons::ButtonType::Inst3);				break;
			case '4':				buttons.toggleButton(mqLib::Buttons::ButtonType::Inst4);				break;
			case '5':				buttons.toggleButton(mqLib::Buttons::ButtonType::Down);					break;
			case '6':				buttons.toggleButton(mqLib::Buttons::ButtonType::Left);					break;
			case '7':				buttons.toggleButton(mqLib::Buttons::ButtonType::Right);				break;
			case '8':				buttons.toggleButton(mqLib::Buttons::ButtonType::Up);					break;
			case 'q':				buttons.toggleButton(mqLib::Buttons::ButtonType::Global);				break;
			case 'w':				buttons.toggleButton(mqLib::Buttons::ButtonType::Multi);				break;
			case 'e':				buttons.toggleButton(mqLib::Buttons::ButtonType::Edit);					break;
			case 'r':				buttons.toggleButton(mqLib::Buttons::ButtonType::Sound);				break;
			case 't':				buttons.toggleButton(mqLib::Buttons::ButtonType::Shift);				break;
			case 'y':				buttons.toggleButton(mqLib::Buttons::ButtonType::Power);				break;
			case 'z':				buttons.toggleButton(mqLib::Buttons::ButtonType::Multimode);			break;
			case 'u':				buttons.toggleButton(mqLib::Buttons::ButtonType::Peek);					break;
			case 'i':				buttons.toggleButton(mqLib::Buttons::ButtonType::Play);					break;
			case 's':				buttons.rotate(mqLib::Buttons::Encoders::LcdLeft, 1);					break;
			case 'x':				buttons.rotate(mqLib::Buttons::Encoders::LcdLeft, -1);					break;
			case 'd':				buttons.rotate(mqLib::Buttons::Encoders::LcdRight, 1);					break;
			case 'c':				buttons.rotate(mqLib::Buttons::Encoders::LcdRight, -1);					break;
			case 'f':				buttons.rotate(mqLib::Buttons::Encoders::Master, 1);					break;
			case 'v':				buttons.rotate(mqLib::Buttons::Encoders::Master, -1);					break;
			case 'g':				buttons.rotate(mqLib::Buttons::Encoders::Matrix1, 1);					break;
			case 'b':				buttons.rotate(mqLib::Buttons::Encoders::Matrix1, -1);					break;
			case 'h':				buttons.rotate(mqLib::Buttons::Encoders::Matrix2, 1);					break;
			case 'n':				buttons.rotate(mqLib::Buttons::Encoders::Matrix2, -1);					break;
			case 'j':				buttons.rotate(mqLib::Buttons::Encoders::Matrix3, 1);					break;
			case 'm':				buttons.rotate(mqLib::Buttons::Encoders::Matrix3, -1);					break;
			case 'k':				buttons.rotate(mqLib::Buttons::Encoders::Matrix4, 1);					break;
			case ',':				buttons.rotate(mqLib::Buttons::Encoders::Matrix4, -1);					break;
			case '9':
				// Midi Note On
				mc->getQSM().writeSciRX(0x90);
				mc->getQSM().writeSciRX(60);
				mc->getQSM().writeSciRX(0x7f);
				break;
			case '0':	
				// Midi Note Off
				mc->getQSM().writeSciRX(0x80);
				mc->getQSM().writeSciRX(60);
				mc->getQSM().writeSciRX(0x7f);
				break;
			case '!':
				dsp->dumpPMem("mq_dump_P_" + std::to_string(dspCycles));
				break;
			default:																						break;
			}
			ch = 0;
		}

		const uint32_t hf01 = (hdiUC.icr() >> 3) & 3;

		if(hf01 != hdiHF01)
		{
//			LOG("HDI HF01=" << HEXN(hf01,1));
			hdiHF01 = hf01;
		}

		injectUCtoDSPInterrupts();

		// transfer DSP host flags HF2&3 to uc
		auto isr = hdiUC.isr();
		const auto prevIsr = isr;
		isr &= ~0x18;
		isr |= (hdiHF23<<3);
		if(isr != prevIsr)
			hdiUC.isr(isr);

		hdiTransferUCtoDSP();
		hdiTransferDSPtoUC();

		if(dumpDSP)
		{
			dsp->dumpPMem("mq_dump_P_" + std::to_string(dspCycles));
			dumpDSP = false;
		}

		if(mc->requestDSPReset())
		{
			if(haveSentTXtoDSP)
				mc->dumpMemory("DSPreset");
			assert(!haveSentTXtoDSP && "DSP needs reset even though it got data already. Needs impl");
			mc->notifyDSPBooted();
		}

		processAudio();
	}

	return 0;
}
