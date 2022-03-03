#include <iostream>

#include "../virusConsoleLib/consoleApp.h"
#include "../virusConsoleLib/esaiListenerToFile.h"

#include "../dsp56300/source/dsp56kEmu/jitunittests.h"

#include "../synthLib/os.h"
#include "../synthLib/wavReader.h"

namespace synthLib
{
	class WavReader;
}

int main(int _argc, char* _argv[])
{
	if constexpr (true)
	{
		try
		{
			puts("Running Unit Tests...");
//			dsp56k::InterpreterUnitTests tests;
			dsp56k::JitUnittests jitTests;
			puts("Unit Tests finished.");
		}
		catch (const std::string& _err)
		{
			std::cout << "Unit test failed: " << _err << std::endl;
			return -1;
		}
	}

	if(_argc < 3)
	{
		std::cout << "Usage: romfile presetname" << std::endl;
		return -1;
	}

	const auto romFile = _argv[1];
	const auto preset = _argv[2];

	ConsoleApp app(romFile, 0x100000, 0x020000);

	if(!app.isValid())
	{
		std::cout << "Failed to load ROM " << romFile << ", make sure that the ROM file is valid" << std::endl;
		return -1;
	}

	if (!app.loadSingle(preset))
	{
		std::cout << "Failed to find preset '" << _argv[1] << "', make sure to use a ROM that contains it" << std::endl;
		ConsoleApp::waitReturn();
		return -1;
	}

	const auto compareFilename = app.getSingleNameAsFilename();

	const auto hFile = fopen(compareFilename.c_str(), "rb");
	if(!hFile)
	{
		std::cout << "Failed to load wav file " << compareFilename << " for comparison" << std::endl;
		return -1;
	}
	fseek(hFile, 0, SEEK_END);
	const auto size = ftell(hFile);
	std::vector<uint8_t> fileData;
	fileData.resize(size);
	fseek(hFile, 0, SEEK_SET);
	if(fread(&fileData.front(), 1, size, hFile) != size)
	{
		std::cout << "Failed to read data from file " << compareFilename << std::endl;
		fclose(hFile);
		return -1;
	}
	fclose(hFile);

	synthLib::Data wavData;

	if(!synthLib::WavReader::load(wavData, nullptr, &fileData.front(), fileData.size()))
	{
		std::cout << "Failed to interpret file " << compareFilename << " as wave data, make sure that the file is a valid 24 bit stereo wav file" << std::endl;
		return -1;
	}

	if(wavData.bitsPerSample != 24 || wavData.channels != 2 || wavData.isFloat)
	{
		std::cout << "Wave file " << compareFilename << " has an invalid format, expected 24 bit / 2 channels but got " << wavData.bitsPerSample << " bit / " << wavData.channels << " channels" << std::endl;
		return -1;
	}

	const auto sampleCount = wavData.dataByteSize / wavData.bitsPerSample;

	uint32_t offset = 0;

	std::vector<uint8_t> temp;
	const auto* const compareData = static_cast<const uint8_t*>(wavData.data);

	int32_t errorPosition = -1;

	app.run([&](const std::vector<dsp56k::TWord>& _data)
	{
		if (errorPosition != -1)
			return true;

		temp.clear();

		for (const auto& d : _data)
			EsaiListenerToFile::writeWord(temp, d);

		for (const auto& d : temp)
		{
			if(d == compareData[offset++])
				continue;

			errorPosition = static_cast<int32_t>(offset / 6);
			std::cout << "Audio output differs at frame position " << errorPosition << std::endl;
		}
		return true;
	}, static_cast<uint32_t>(sampleCount));

	if(errorPosition == -1)
		return 0;

	std::cout << "Test failed, audio output is not identical to reference file, difference starting at frame " << errorPosition << std::endl;
	return -2;
}
