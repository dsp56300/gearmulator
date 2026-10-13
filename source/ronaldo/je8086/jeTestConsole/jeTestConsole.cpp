#include <algorithm>
#include <iostream>

#include "jeLib/device.h"
#include "jeLib/je8086.h"
#include "jeLib/romloader.h"
#include "synthLib/consoleTools.h"

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

		constexpr uint32_t blockSize = 128;
		synthLib::DeviceRunner runner(device, blockSize);

		bool bootFinished = false;
		bool demoRunning = false;

		runner.getFrontPanel().evLcdText.addListener([&](uint32_t, uint32_t, const std::vector<uint8_t>& _lcdContent)
		{
			char lcdString[41]{0};

			for (size_t i=0; i<std::min<size_t>(_lcdContent.size(), 40); ++i)
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
			runner.process();

		synthLib::StereoWavSink wav("je8086_out.wav", samplerate);
		synthLib::RealtimeMeter meter(samplerate);

		runner.setWavSink(&wav);

		while (true)
		{
			runner.process();
			meter.add(blockSize);
		}
	}
	catch (std::exception& e)
	{
		std::cerr << "Error: " << e.what() << "\n";
		return -1;
	}

	return 0;
}
