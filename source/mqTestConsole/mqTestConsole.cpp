#include <iostream>
#include <memory>

#include "../synthLib/os.h"
#include "../synthLib/wavWriter.h"

#include "../mqLib/mqmc.h"
#include "../mqLib/mqhardware.h"

#include "dsp56kEmu/dspthread.h"
#include "dsp56kEmu/interpreterunittests.h"
#include "dsp56kEmu/jitunittests.h"

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

int main(int _argc, char* _argv[])
{
	try
	{
//		dsp56k::InterpreterUnitTests tests;
		dsp56k::JitUnittests tests;
//		return 0;
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

	std::unique_ptr<mqLib::Hardware> hw;
	hw.reset(new mqLib::Hardware(romFile));

	auto& buttons = hw->getButtons();
	auto& qsm = hw->getUC().getQSM();

	char ch = 0;
	std::thread inputReader([&ch, &hw]
	{
		while(hw.get())
		{
			if(_kbhit())
				ch = static_cast<char>(_getch());
		}
	});
	
	bool silence = true;

	const std::string filename = "mq_output_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) + ".wav";
	synthLib::AsyncWriter wavWriter(filename, 44100);

	constexpr uint32_t blockSize = 64;

	std::vector<dsp56k::TWord> m_stereoOutput;
	m_stereoOutput.resize(blockSize<<1);

	while(true)
	{
		if(ch)
		{
			switch (ch)
			{
			case '1':				buttons.toggleButton(mqLib::Buttons::ButtonType::Inst1);			break;
			case '2':				buttons.toggleButton(mqLib::Buttons::ButtonType::Inst2);			break;
			case '3':				buttons.toggleButton(mqLib::Buttons::ButtonType::Inst3);			break;
			case '4':				buttons.toggleButton(mqLib::Buttons::ButtonType::Inst4);			break;
			case '5':				buttons.toggleButton(mqLib::Buttons::ButtonType::Down);				break;
			case '6':				buttons.toggleButton(mqLib::Buttons::ButtonType::Left);				break;
			case '7':				buttons.toggleButton(mqLib::Buttons::ButtonType::Right);			break;
			case '8':				buttons.toggleButton(mqLib::Buttons::ButtonType::Up);				break;
			case 'q':				buttons.toggleButton(mqLib::Buttons::ButtonType::Global);			break;
			case 'w':				buttons.toggleButton(mqLib::Buttons::ButtonType::Multi);			break;
			case 'e':				buttons.toggleButton(mqLib::Buttons::ButtonType::Edit);				break;
			case 'r':				buttons.toggleButton(mqLib::Buttons::ButtonType::Sound);			break;
			case 't':				buttons.toggleButton(mqLib::Buttons::ButtonType::Shift);			break;
			case 'y':				buttons.toggleButton(mqLib::Buttons::ButtonType::Power);			break;
			case 'z':				buttons.toggleButton(mqLib::Buttons::ButtonType::Multimode);		break;
			case 'u':				buttons.toggleButton(mqLib::Buttons::ButtonType::Peek);				break;
			case 'i':				buttons.toggleButton(mqLib::Buttons::ButtonType::Play);				break;
			case 's':				buttons.rotate(mqLib::Buttons::Encoders::LcdLeft, 1);				break;
			case 'x':				buttons.rotate(mqLib::Buttons::Encoders::LcdLeft, -1);				break;
			case 'd':				buttons.rotate(mqLib::Buttons::Encoders::LcdRight, 1);				break;
			case 'c':				buttons.rotate(mqLib::Buttons::Encoders::LcdRight, -1);				break;
			case 'f':				buttons.rotate(mqLib::Buttons::Encoders::Master, 1);				break;
			case 'v':				buttons.rotate(mqLib::Buttons::Encoders::Master, -1);				break;
			case 'g':				buttons.rotate(mqLib::Buttons::Encoders::Matrix1, 1);				break;
			case 'b':				buttons.rotate(mqLib::Buttons::Encoders::Matrix1, -1);				break;
			case 'h':				buttons.rotate(mqLib::Buttons::Encoders::Matrix2, 1);				break;
			case 'n':				buttons.rotate(mqLib::Buttons::Encoders::Matrix2, -1);				break;
			case 'j':				buttons.rotate(mqLib::Buttons::Encoders::Matrix3, 1);				break;
			case 'm':				buttons.rotate(mqLib::Buttons::Encoders::Matrix3, -1);				break;
			case 'k':				buttons.rotate(mqLib::Buttons::Encoders::Matrix4, 1);				break;
			case ',':				buttons.rotate(mqLib::Buttons::Encoders::Matrix4, -1);				break;
			case '9':
				// Midi Note On
				qsm.writeSciRX(0x90);
				qsm.writeSciRX(60);
				qsm.writeSciRX(0x7f);
				break;
			case '0':	
				// Midi Note Off
				qsm.writeSciRX(0x80);
				qsm.writeSciRX(60);
				qsm.writeSciRX(0x7f);
				break;
			case 'o':
				// Modwheel Max
				qsm.writeSciRX(0xb0);
				qsm.writeSciRX(1);
				qsm.writeSciRX(0x7f);
				break;
			case 'p':	
				// Modwheel Min
				qsm.writeSciRX(0xb0);
				qsm.writeSciRX(1);
				qsm.writeSciRX(0x0);
				break;
			case '!':
				hw->getDSP().dumpPMem("dsp_dump_P_" + std::to_string(hw->getDspCycles()));
				break;
			case '&':
				hw->getDSP().dumpXYMem("dsp_dump_mem_" + std::to_string(hw->getDspCycles()) + "_");
				break;
			case '"':
				hw->getUC().dumpMemory("mc_dump_mem");
				break;
			case '$':
				hw->getUC().dumpROM("rom_runtime");
				break;
			default:
				break;
			}
			ch = 0;
		}

		hw->process(blockSize);

		auto& outputs = hw->getAudioOutputs();

		for(size_t i=0; i<blockSize; ++i)
		{
			m_stereoOutput[i<<1] = outputs[0][i];
			m_stereoOutput[(i<<1) + 1] = outputs[1][i];

			if(silence && (outputs[0][i] || outputs[1][i]))
				silence = false;
		}

		if(!silence)
		{
			wavWriter.append([&](auto& _dst)
			{
				_dst.reserve(_dst.size() + m_stereoOutput.size());
				for (auto& d : m_stereoOutput)
					_dst.push_back(d);
			}
			);
		}
	}
	
	return 0;
}
