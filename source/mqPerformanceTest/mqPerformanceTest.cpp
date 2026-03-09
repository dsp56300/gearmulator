#include <iostream>

#include "synthLib/wavWriter.h"

#include "mqLib/microq.h"
#include "mqLib/mqhardware.h"

#include "dsp56kEmu/jitunittests.h"
#include "dsp56kBase/threadtools.h"

#include <vector>

#include "mqConsoleLib/mqSettingsGui.h"

#ifdef _WIN32
#include <crtdbg.h>
#include <csignal>
#endif

using ButtonType = mqLib::Buttons::ButtonType;
using EncoderType = mqLib::Buttons::Encoders;

int main(int _argc, char* _argv[])
{
#ifdef _WIN32
	// Redirect debug assertions to stderr instead of popup dialog
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
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
		std::cin.ignore();
		return -2;
	}

	dsp56k::ThreadTools::setCurrentThreadName("main");

	// WAV writer for main stereo output
	synthLib::AsyncWriter writer("mqOutput.wav", 44100, false);

	constexpr uint32_t blockSize = 64;
	std::vector<dsp56k::TWord> stereoBuf(blockSize * 2);

	bool waitingForBoot = true;
	bool waitingForInitSound = false;
	int counter = -1;

	std::cout << "Device booting..." << std::endl;

	mq.setButton(ButtonType::Play, true);

	std::array<char,40> lastLcdContent{};
	constexpr int lcdPrintDelay = 5000 / blockSize;
	int lcdPrintTimer = -1;
	bool foundEndText = false;

	// Helper to check LCD content
	auto getLcdLine = [&](int line) -> std::string
	{
		const auto& lcdData = mq.getHardware()->getUC().getLcd().getDdRam();
		auto printData = lcdData;
		for (char& c : printData)
		{
			const auto u = static_cast<uint8_t>(c);
			if(u < 32 || u > 127)
				c = '?';
		}
		return std::string(&printData[line * 20], 20);
	};

	bool demoStarted = false;
	int playRetryCounter = 0;

	while(true)
	{

		if(waitingForBoot && mq.isBootCompleted())
		{
			std::cout << "Boot completed" << std::endl;

			waitingForBoot = false;
			mq.setButton(ButtonType::Play, false);

			// In VE mode boot takes longer; wait for "Init Sound" on LCD
			waitingForInitSound = true;
		}

		if(waitingForInitSound)
		{
			const auto lineB = getLcdLine(1);
			if(lineB.find("Init Sound") != std::string::npos)
			{
				std::cout << "Init Sound detected, starting demo sequence" << std::endl;
				waitingForInitSound = false;
				counter = 80000 / blockSize;
			}
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

				mq.getHardware()->getDspThread(0).setLogToStdout(true);
#if MQ_VOICE_EXPANSION
				mq.getHardware()->getDspThread(1).setLogToStdout(true);
				mq.getHardware()->getDspThread(2).setLogToStdout(true);
#endif
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

				// In VE mode, the demo prompt may appear after Play was already
				// pressed. Detect the prompt and press Play again.
				if(!demoStarted && lineA.find("Press <Play>") != std::string::npos)
				{
					std::cout << "Demo prompt detected, pressing Play again" << std::endl;
					mq.setButton(ButtonType::Play, true);
					playRetryCounter = 5000 / blockSize;
				}

				const auto hasEndText = lineA.find(" ...DEEPER ") != std::string::npos;

				if(hasEndText)
					foundEndText = true;
				else if(foundEndText)
					break;
			}

			// Release Play after retry press
			if(playRetryCounter > 0 && --playRetryCounter == 0)
			{
				mq.setButton(ButtonType::Play, false);
				demoStarted = true;
			}
		}

		mq.process(blockSize, 0);

		if(counter == 0)
		{
			auto& outs = mq.getAudioOutputs();

			for(size_t i=0; i<blockSize; ++i)
			{
				stereoBuf[(i<<1)  ] = outs[0][i];
				stereoBuf[(i<<1)+1] = outs[1][i];
			}

			writer.append([&stereoBuf](std::vector<dsp56k::TWord>& _wavOut)
			{
				_wavOut.insert(_wavOut.end(), stereoBuf.begin(), stereoBuf.end());
			});
		}
	}

	std::cout << '\a';
	std::cin.ignore();

	return 0;
}
