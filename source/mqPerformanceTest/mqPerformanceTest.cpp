#include <fstream>
#include <iostream>
#include <memory>

#include "../synthLib/wavWriter.h"

#include "../mqLib/microq.h"
#include "../mqLib/mqhardware.h"

#include "dsp56kEmu/dspthread.h"

#include "dsp56kEmu/jitunittests.h"

#include <vector>

#include "../mqConsoleLib/mqSettingsGui.h"

using ButtonType = mqLib::Buttons::ButtonType;
using EncoderType = mqLib::Buttons::Encoders;

int main(int _argc, char* _argv[])
{
	std::cout << "Running unit tests..." << std::endl;

	try
	{
//		dsp56k::InterpreterUnitTests tests;		// only valid if Interpreter is active
		dsp56k::JitUnittests tests;				// only valid if JIT runtime is active
//		return 0;
	}
	catch(std::string& _err)
	{
		std::cout << "Unit Test failed: " << _err << std::endl;
		return -1;
	}

	std::cout << "Unit tests passed" << std::endl;

	// create hardware
	mqLib::MicroQ mq(mqLib::BootMode::Default);

	if(!mq.isValid())
	{
		std::cout << "Failed to find OS update midi file. Put mq_2_23.mid next to this program" << std::endl;
		return -2;
	}

	dsp56k::DSPThread::setCurrentThreadName("main");

	synthLib::AsyncWriter writer("mqOutput.wav", 44100, false);

	constexpr uint32_t blockSize = 64;
	std::vector<dsp56k::TWord> stereoOutput;
	stereoOutput.resize(blockSize * 2);

	bool waitingForBoot = true;
	int counter = -1;

	std::cout << "Device booting..." << std::endl;

	mq.setButton(ButtonType::Play, true);

	std::array<char,40> lastLcdContent{};
	constexpr int lcdPrintDelay = 5000 / blockSize;
	int lcdPrintTimer = -1;
	bool foundEndText = false;

	while(true)
	{
		if(waitingForBoot && mq.isBootCompleted())
		{
			std::cout << "Boot completed" << std::endl;

			waitingForBoot = false;
			mq.setButton(ButtonType::Play, false);
			counter = 80000 / blockSize;
		}

		if(counter > 0)
		{
			--counter;
			if(counter == 50000 / blockSize)
			{
				mq.setButton(ButtonType::Multimode, true);
				std::cout << "Pressing Multimode + Peek" << std::endl;
			}
			if(counter == 40000 / blockSize)
			{
				mq.setButton(ButtonType::Peek, true);
			}
			else if(counter == (20000 / blockSize))
			{
				mq.setButton(ButtonType::Peek, false);
				mq.setButton(ButtonType::Multimode, false);
			}
			else if(counter == (10000 / blockSize))
			{
				mq.setButton(ButtonType::Play, true);
				std::cout << "Pressing Play to start demo playback" << std::endl;
			}
			else if(counter == 0)
			{
				mq.setButton(ButtonType::Play, false);

				mq.getHardware()->getDspThread().setLogToStdout(true);
			}
		}
		else
		{
			const auto& lcdData = mq.getHardware()->getUC().getLcd().getDdRam();
			if(lcdData != lastLcdContent)
			{
				lcdPrintTimer = lcdPrintDelay;
				lastLcdContent = lcdData;
			}
			else if(lcdPrintTimer > 0 && --lcdPrintTimer == 0)
			{
				auto printData = lcdData;
				for (char& c : printData)
				{
					const auto u = static_cast<uint8_t>(c);
					if(u < 32 || u > 127)
						c = '?';
				}

				const auto lineA = std::string(&printData[0], 20);
				const auto lineB = std::string(&printData[20], 20);
				std::cout << "LCD: '" << lineA << "'" << std::endl << "LCD: '" << lineB << "'" << std::endl;

				const auto hasEndText = lineA.find(" ...DEEPER ") != std::string::npos;

				if(hasEndText)
					foundEndText = true;
				else if(foundEndText)
					break;
			}
		}

		mq.process(blockSize, 0);

		if(counter == 0)
		{
			auto& outs = mq.getAudioOutputs();

			for(size_t i=0; i<blockSize; ++i)
			{
				stereoOutput[(i<<1)  ] = outs[0][i];
				stereoOutput[(i<<1)+1] = outs[1][i];
			}

			writer.append([&](std::vector<dsp56k::TWord>& _wavOut)
			{
				_wavOut.insert(_wavOut.end(), stereoOutput.begin(), stereoOutput.end());
			});
		}
	}

	std::cout << '\a';

	std::cout << "Demo Playback has ended, press key to exit" << std::endl;

	std::cin.ignore();

	return 0;
}
