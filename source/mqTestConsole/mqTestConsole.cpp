#include <memory>

#include "../mqLib/mqdsp.h"
#include "../synthLib/os.h"

#include "../mqLib/rom.h"
#include "../mqLib/mqmc.h"
#include "dsp56kEmu/dspthread.h"

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

	uint32_t hdiHF01 = 0;	// DSP => uc
	uint32_t hdiHF23 = 0;	// uc => DSP
	uint64_t dspCycles = 0;

	dspThread.setCallback([&](uint32_t)
	{
		// transfer HF 2&3 from uc to DSP
		auto hcr = hdiDSP.readControlRegister();
		hcr &= ~0x18;
		hcr |= (hdiHF23<<3);
		hdiDSP.writeControlRegister(hcr);

		const auto hf01 = (hdiDSP.readStatusRegister() >> 3) & 3;
		if(hf01 != hdiHF01)
		{
			LOG("HDI HF01=" << HEXN(hf01,1));
			hdiHF01 = hf01;
		}
	});

	uint32_t prevInstructions = 0;

	while(dsp.get())
	{
		const auto instructionCounter = dsp->dsp().getInstructionCounter();
		const auto d = dsp56k::delta(instructionCounter, prevInstructions);
		prevInstructions = instructionCounter;
		dspCycles += d;

		if(mc->getCycles() > dspCycles/6)
		{
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
			default:																						break;
			}
			ch = 0;
		}

		const uint32_t hf23 = (hdiUC.icr() >> 3) & 3;

		if(hf23 != hdiHF23)
		{
			LOG("HDI HF23=" << HEXN(hf23,1));
			hdiHF23 = hf23;
		}

		// transfer DSP host flags HF0&1 to uc
		auto icr = hdiUC.icr();
		icr &= ~0x18;
		icr |= (hdiHF01<<3);
		hdiUC.icr(icr);

		hdiUC.pollTx(txData);

		for (const uint32_t data : txData)
			hdiDSP.writeRX(&data, 1);

		uint8_t interruptAddr;
		if(hdiUC.pollInterruptRequest(interruptAddr))
			dsp->dsp().injectInterrupt(interruptAddr);

		while(hdiDSP.hasTX())
		{
			hdiUC.writeRx(hdiDSP.readTX());
		}

		if(dumpDSP)
		{
			dsp->dsp().coreDump();
			dumpDSP = false;
		}

		txData.clear();
	}

	return 0;
}
