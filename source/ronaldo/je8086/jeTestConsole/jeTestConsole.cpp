#include <iostream>

#include "dsp56kEmu/audio.h"
#include "jeLib/device.h"
#include "jeLib/je8086.h"
#include "jeLib/romloader.h"
#include "synthLib/wavWriter.h"

using namespace jeLib;

int main(int _argc, char* _argv[])
{
	std::cout << "JE-8086 Test Console\n";
	std::cout << "Searching for ROM...\n";

	auto rom = RomLoader::findROM();

	if (!rom.isValid())
	{
		std::cerr << "No valid ROM found.\n";
		return -1;
	}

	std::cout << "ROM found, creating device...\n";

	synthLib::DeviceCreateParams params;

	params.romData = rom.getData();
	params.romName = rom.getName();

	constexpr uint32_t samplerate = 88200;
	params.hostSamplerate = samplerate;
	params.preferredSamplerate = samplerate;

	try
	{
		Device device(params);

		device.setMasterVolume(7.0f);

		std::cout << "Boot done, starting factory demo playback...\n";

		// prepare audio buffers
		std::array<std::vector<float>, 2> outBuffers;

		synthLib::TAudioInputs inputs;
		synthLib::TAudioOutputs outputs;

		constexpr size_t blocksize = 128;

		for (size_t i=0; i<outBuffers.size(); ++i)
		{
			outBuffers[i].resize(blocksize);
			outputs[i] = outBuffers[i].data();
		}

		std::vector<synthLib::SMidiEvent> midiIn;
		std::vector<synthLib::SMidiEvent> midiOut;

		uint64_t sampleCounter = 0;

		bool bootFinished = false;
		bool demoRunning = false;

		SysexRemoteControl sysexRemote;

		sysexRemote.evLcdDdDataChanged.addListener([&](const std::array<char, 40>& _lcdContent)
		{
			char lcdString[41]{0};

			for (size_t i=0; i<_lcdContent.size(); ++i)
				lcdString[i] = _lcdContent[i] >= ' ' ? static_cast<char>(_lcdContent[i]) : ' ';

			std::string s(lcdString);

			std::cout << "LCD: [" << s.substr(0 , 16) << "]\n";
			std::cout << "LCD: [" << s.substr(20, 16) << "]\n";

			if (!bootFinished)
			{
				if (s.find("PERFORM") != std::string::npos)
				{
					bootFinished = true;
					std::cout << "Boot finished, starting demo playback...\n";

					device.getJe8086().setButton(devices::kSwitch_Rec, true);
					device.getJe8086().setButton(devices::kSwitch_Hold, true);
				}
			}
			else if(!demoRunning)
			{
				if (s.find("=== ROM PLAY ===") != std::string::npos)
				{
					demoRunning = true;
					std::cout << "Demo playback started, starting recording to .wav file.\n";
				}
			}
		});


		while (!demoRunning)
		{
			device.process(inputs, outputs, blocksize, midiIn, midiOut);

			for (const auto& e : midiOut)
				sysexRemote.receive(e);
			midiOut.clear();
		}

		synthLib::AsyncWriter writer("je8086_out.wav", samplerate);

		auto t0 = std::chrono::high_resolution_clock::now();
		auto tLast = t0;
		uint64_t totalProcessedSamples = 0;
		uint32_t intervalProcessedSamples = 0;

		while (true)
		{
			device.process(inputs, outputs, blocksize, midiIn, midiOut);

			for (const auto& e : midiOut)
				sysexRemote.receive(e);
			midiOut.clear();

			sampleCounter += blocksize;

			writer.append([&outBuffers, blocksize](std::vector<dsp56k::TWord>& _dst)
			{
				_dst.reserve(_dst.size() + blocksize * 2);

				for (size_t i=0; i<blocksize; ++i)
				{
					_dst.push_back(dsp56k::sample2dsp(outBuffers[0][i]));
					_dst.push_back(dsp56k::sample2dsp(outBuffers[1][i]));
				}
			});

			totalProcessedSamples += blocksize;
			intervalProcessedSamples += blocksize;

			if(intervalProcessedSamples >= samplerate)
			{
				auto tNow = std::chrono::high_resolution_clock::now();
				std::chrono::duration<double> elapsed = tNow - tLast;
				tLast = tNow;
				double intervalRealTimePercent = 100.0f * static_cast<double>(intervalProcessedSamples) / (elapsed.count() * params.hostSamplerate);
				double realTimePercent = 100.0f * static_cast<double>(totalProcessedSamples) / (std::chrono::duration<double>(tNow - t0).count() * params.hostSamplerate);
				std::cout << "Recorded " << (totalProcessedSamples / samplerate) << " sec"
						  << ", last interval speed " << static_cast<int>(intervalRealTimePercent) << "%, "
						  << "total average " << static_cast<int>(realTimePercent) << "%\n";
				intervalProcessedSamples -= samplerate;
			}
		}
	}
	catch (std::exception& e)
	{
		std::cerr << "Error: " << e.what() << "\n";
		return -1;
	}

	return 0;
}
