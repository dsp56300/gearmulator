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

	// WAV writers for all output channels
	// A's 6 TX register stereo pairs
	synthLib::AsyncWriter writer0("mqOutput.wav", 44100, false);      // A pair 0 (main out)
	synthLib::AsyncWriter writer1("mqOutput_A1.wav", 44100, false);   // A pair 1
	synthLib::AsyncWriter writer2("mqOutput_A2.wav", 44100, false);   // A pair 2
	synthLib::AsyncWriter writer3("mqOutput_A3.wav", 44100, false);   // A pair 3
	synthLib::AsyncWriter writer4("mqOutput_A4.wav", 44100, false);   // A pair 4
	synthLib::AsyncWriter writer5("mqOutput_A5.wav", 44100, false);   // A pair 5
	// B/C expansion ring buffer captures
	synthLib::AsyncWriter writerB("mqOutput_B.wav", 44100, false);
	synthLib::AsyncWriter writerC("mqOutput_C.wav", 44100, false);

	constexpr uint32_t blockSize = 64;
	// Stereo buffers for each output pair
	std::vector<dsp56k::TWord> stereoBuf[8];
	for (auto& buf : stereoBuf)
		buf.resize(blockSize * 2);

	uint32_t nonZeroSamples = 0;
	uint32_t totalSamples = 0;

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

	uint32_t loopCount = 0;
	bool demoStarted = false;
	int playRetryCounter = 0;

	while(true)
	{
		++loopCount;

		// Safety: abort if stuck (>10 million iterations without audio)
		if(loopCount > 10000000 && totalSamples == 0)
		{
			std::cout << "ABORT: No audio after " << loopCount << " iterations" << std::endl;
			break;
		}

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

		// Periodic state logging
		if(loopCount % 100000 == 0)
		{
			std::cout << "[Loop " << loopCount << "] counter=" << counter
				<< " waitBoot=" << waitingForBoot << " waitInit=" << waitingForInitSound
				<< " totalSamples=" << totalSamples << std::endl;
		}

		if(counter == 0)
		{
			auto& outs = mq.getAudioOutputs();

			uint32_t blockNonZero = 0;
			for(size_t i=0; i<blockSize; ++i)
			{
				// A's 6 TX register stereo pairs (audioOutputs 0-11)
				// Note: audioOutputs use swapped L/R mapping, so pair N = outs[2N] (R), outs[2N+1] (L)
				for (int p = 0; p < 6; ++p)
				{
					stereoBuf[p][(i<<1)  ] = outs[p*2  ][i];
					stereoBuf[p][(i<<1)+1] = outs[p*2+1][i];
				}
				// B expansion = audioOutputs 12/13, C expansion = audioOutputs 14/15
				stereoBuf[6][(i<<1)  ] = outs[12][i];
				stereoBuf[6][(i<<1)+1] = outs[13][i];
				stereoBuf[7][(i<<1)  ] = outs[14][i];
				stereoBuf[7][(i<<1)+1] = outs[15][i];

				if(outs[0][i] != 0 || outs[1][i] != 0)
					++blockNonZero;
			}
			totalSamples += blockSize;
			nonZeroSamples += blockNonZero;

			synthLib::AsyncWriter* writers[] = {&writer0, &writer1, &writer2, &writer3, &writer4, &writer5, &writerB, &writerC};
			for (int w = 0; w < 8; ++w)
			{
				const int idx = w;
				writers[w]->append([&stereoBuf, idx](std::vector<dsp56k::TWord>& _wavOut)
				{
					_wavOut.insert(_wavOut.end(), stereoBuf[idx].begin(), stereoBuf[idx].end());
				});
			}

			// Log audio stats every ~2 seconds
			if(totalSamples > 0 && (totalSamples % (44100 * 2)) < blockSize)
			{
				std::cout << "Audio: nonZero=" << nonZeroSamples << "/" << totalSamples
					<< " (" << (nonZeroSamples * 100 / totalSamples) << "%)" << std::endl;
			}

			// Safety exit after ~60 seconds of audio
			if(totalSamples > 44100 * 60 * 10)
			{
				std::cout << "Timeout after 60s of audio capture" << std::endl;
				break;
			}
		}
	}

	std::cout << "Final audio stats: nonZero=" << nonZeroSamples << "/" << totalSamples
		<< " (" << (totalSamples > 0 ? (nonZeroSamples * 100 / totalSamples) : 0) << "%)" << std::endl;

	return 0;
}
