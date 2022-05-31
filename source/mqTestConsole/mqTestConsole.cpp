#include <memory>

#include "../mqLib/mqdsp.h"
#include "../synthLib/os.h"

#include "../mqLib/rom.h"
#include "../mqLib/mqmc.h"
#include "dsp56kEmu/dspthread.h"

#ifdef __APPLE_CC__
#include <stdio.h>
#include <sys/select.h>
#include <termios.h>

int _kbhit() {
	static const int STDIN = 0; static bool init=false;
	if (!init) {termios term;tcgetattr(STDIN,&term);term.c_lflag&=~ICANON;tcsetattr(STDIN,TCSANOW,&term);setbuf(stdin,0);init=true;}
	timeval timeout;fd_set rdset;FD_ZERO(&rdset);FD_SET(STDIN, &rdset);timeout.tv_sec  = 0;timeout.tv_usec = 0;
	return select(STDIN + 1, &rdset, NULL, NULL, &timeout);
}
#define _getch() getchar()
#else
#include <conio.h>
#endif

namespace mqLib
{
	class MqDsp;
}

#define DSP 1

int main(int _argc, char* _argv[])
{
	const auto romFile = synthLib::findROM(524288);

	mqLib::ROM rom(romFile);

	std::unique_ptr<mqLib::MqMc> mc;
	std::unique_ptr<mqLib::MqDsp> dsp;

	mc.reset(new mqLib::MqMc(rom));
	dsp.reset(new mqLib::MqDsp());

	std::deque<uint32_t> txData;

#if DSP
	dsp56k::DSPThread dspThread(dsp->dsp());
	bool dumpDSP = false;
#endif

	char ch = 0;
	std::thread inputReader([&ch, &dsp]
	{
		while(dsp.get())
		{
			if(_kbhit())
				ch = static_cast<char>(_getch());
		}
	});

	auto& buttons = mc->getButtons();

	while(dsp.get())
	{
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
			case 'y':
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
			default:;
			}
			ch = 0;
		}
#if DSP
		mc->hdi08().pollTx(txData);
		for (const uint32_t data : txData)
			dsp->hdi08().writeRX(&data, 1);

		uint8_t interruptAddr;
		if(mc->hdi08().pollInterruptRequest(interruptAddr))
			dsp->dsp().injectInterrupt(interruptAddr);

		while(dsp->hdi08().hasTX())
		{
			mc->hdi08().writeRx(dsp->hdi08().readTX());
		}

		if(dumpDSP)
		{
			dsp->dsp().coreDump();
			dumpDSP = false;
		}
#endif
		txData.clear();
	}

	return 0;
}
