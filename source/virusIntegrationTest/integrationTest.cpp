#include <iostream>

#include "integrationTest.h"

#include "../virusConsoleLib/consoleApp.h"
#include "../virusConsoleLib/esaiListenerToFile.h"

#include "../dsp56300/source/dsp56kEmu/jitunittests.h"
#include "../dsp56300/source/disassemble/commandline.h"

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

	try
	{
		const CommandLine cmd(_argc, _argv);

		const auto romFile = cmd.get("rom");
		const auto preset = cmd.get("preset");

		IntegrationTest test(cmd);
		return test.run();
	}
	catch(const std::runtime_error& _err)
	{
		std::cout << _err.what() << std::endl;
		return -1;
	}
}

IntegrationTest::IntegrationTest(const CommandLine& _commandLine)
	: m_cmd(_commandLine)
	, m_romFile(_commandLine.get("rom"))
	, m_app(m_romFile, 0x100000, 0x020000)
{
}

int IntegrationTest::run()
{
	if (!m_app.isValid())
	{
		std::cout << "Failed to load ROM " << m_romFile << ", make sure that the ROM file is valid" << std::endl;
		return -1;
	}

	const auto preset = m_cmd.get("preset");

	if (!m_app.loadSingle(preset))
	{
		std::cout << "Failed to find preset '" << preset << "', make sure to use a ROM that contains it" << std::endl;
		ConsoleApp::waitReturn();
		return -1;
	}

	const int lengthSeconds = m_cmd.contains("length") ? m_cmd.getInt("length") : 0;
	if(lengthSeconds > 0)
	{
		// create reference file
		return runCreate(lengthSeconds);
	}
	else
	{
		const auto compareFilename = m_app.getSingleNameAsFilename();

		if (!loadReferenceFile(compareFilename))
			return -1;

		return runCompare();
	}
}

bool IntegrationTest::loadReferenceFile(const std::string& _filename)
{
	const auto hFile = fopen(_filename.c_str(), "rb");
	if (!hFile)
	{
		std::cout << "Failed to load wav file " << _filename << " for comparison" << std::endl;
		return false;
	}
	fseek(hFile, 0, SEEK_END);
	const auto size = ftell(hFile);
	m_referenceFileData.resize(size);
	fseek(hFile, 0, SEEK_SET);
	if (fread(&m_referenceFileData.front(), 1, size, hFile) != size)
	{
		std::cout << "Failed to read data from file " << _filename << std::endl;
		fclose(hFile);
		return false;
	}
	fclose(hFile);

	m_referenceData.data = nullptr;

	if (!synthLib::WavReader::load(m_referenceData, nullptr, &m_referenceFileData.front(), m_referenceFileData.size()))
	{
		std::cout << "Failed to interpret file " << _filename << " as wave data, make sure that the file is a valid 24 bit stereo wav file" << std::endl;
		return false;
	}

	if(m_referenceData.samplerate != m_app.getRom().getSamplerate())
	{
		std::cout << "Wave file " << _filename << " does not have the correct samplerate, expected " << m_app.getRom().getSamplerate() << " but got " << m_referenceData.samplerate << " instead" << std::endl;
		return false;
	}

	if (m_referenceData.bitsPerSample != 24 || m_referenceData.channels != 2 || m_referenceData.isFloat)
	{
		std::cout << "Wave file " << _filename << " has an invalid format, expected 24 bit / 2 channels but got " << m_referenceData.bitsPerSample << " bit / " << m_referenceData.channels << " channels" << std::endl;
		return false;
	}
	return true;
}

int IntegrationTest::runCompare()
{
	const auto sampleCount = m_referenceData.dataByteSize * 8 / m_referenceData.bitsPerSample;

	uint32_t offset = 0;

	std::vector<uint8_t> temp;
	const auto* const compareData = static_cast<const uint8_t*>(m_referenceData.data);

	int32_t errorPosition = -1;

	m_app.run([&](const std::vector<dsp56k::TWord>& _data)
	{
		if (errorPosition != -1)
			return true;

		temp.clear();

		for (const auto& d : _data)
			EsaiListenerToFile::writeWord(temp, d);

		for (const auto& d : temp)
		{
			if (d == compareData[offset++])
				continue;

			errorPosition = static_cast<int32_t>(offset / 6);
			std::cout << "Audio output differs at frame position " << errorPosition << std::endl;
			break;
		}
		return true;
	}, static_cast<uint32_t>(sampleCount));

	if (errorPosition == -1)
	{
		std::cout << "Test succeeded, compared " << sampleCount << " samples" << std::endl;
		return 0;
	}

	std::cout << "Test failed, audio output is not identical to reference file, difference starting at frame " << errorPosition << std::endl;
	return -2;
}

int IntegrationTest::runCreate(int _lengthSeconds)
{
	const auto sampleCount = m_app.getRom().getSamplerate() * _lengthSeconds * 2;

	const auto filename = m_app.getSingleNameAsFilename();

	auto* hFile = fopen(filename.c_str(), "wb");

	if(!hFile)
	{
		std::cout << "Failed to create output file " << filename << std::endl;
		return -1;
	}

	fclose(hFile);

	m_app.run(filename, sampleCount);

	if(!loadReferenceFile(filename))
	{
		std::cout << "Failed to open written file " << filename << " for verification" << std::endl;
		return -1;
	}

	const auto referenceSampleCount = m_referenceData.dataByteSize * 8 / m_referenceData.bitsPerSample;

	if(referenceSampleCount != sampleCount)
	{
		std::cout << "Verification of written file failed, expected " << sampleCount << " samples but file only has " << referenceSampleCount << " samples" << std::endl;
		return -1;
	}
	return 0;
}
