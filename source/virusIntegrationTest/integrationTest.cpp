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
			dsp56k::JitUnittests jitTests(false);
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
		const auto referenceFile = m_app.getSingleNameAsFilename();

		if (!loadAudioFile(m_referenceFile, referenceFile))
			return -1;

		return runCompare();
	}
}

bool IntegrationTest::loadAudioFile(File& _dst, const std::string& _filename) const
{
	const auto hFile = fopen(_filename.c_str(), "rb");
	if (!hFile)
	{
		std::cout << "Failed to load wav file " << _filename << " for comparison" << std::endl;
		return false;
	}
	fseek(hFile, 0, SEEK_END);
	const auto size = ftell(hFile);
	_dst.file.resize(size);
	fseek(hFile, 0, SEEK_SET);
	if (fread(&_dst.file.front(), 1, size, hFile) != size)
	{
		std::cout << "Failed to read data from file " << _filename << std::endl;
		fclose(hFile);
		return false;
	}
	fclose(hFile);

	_dst.data.data = nullptr;

	if (!synthLib::WavReader::load(_dst.data, nullptr, &_dst.file.front(), _dst.file.size()))
	{
		std::cout << "Failed to interpret file " << _filename << " as wave data, make sure that the file is a valid 24 bit stereo wav file" << std::endl;
		return false;
	}

	if(_dst.data.samplerate != m_app.getRom().getSamplerate())
	{
		std::cout << "Wave file " << _filename << " does not have the correct samplerate, expected " << m_app.getRom().getSamplerate() << " but got " << _dst.data.samplerate << " instead" << std::endl;
		return false;
	}

	if (_dst.data.bitsPerSample != 24 || _dst.data.channels != 2 || _dst.data.isFloat)
	{
		std::cout << "Wave file " << _filename << " has an invalid format, expected 24 bit / 2 channels but got " << _dst.data.bitsPerSample << " bit / " << _dst.data.channels << " channels" << std::endl;
		return false;
	}
	return true;
}

int IntegrationTest::runCompare()
{
	const auto sampleCount = m_referenceFile.data.dataByteSize * 8 / m_referenceFile.data.bitsPerSample;

	uint32_t offset = 0;

	std::vector<uint8_t> temp;
	const auto* const compareData = static_cast<const uint8_t*>(m_referenceFile.data.data);

	int32_t errorPosition = -1;

	File compareFile;
	const auto res = createAudioFile(compareFile, "compare_", static_cast<uint32_t>(sampleCount));
	if(res)
		return res;

	auto* ptrA = static_cast<const uint8_t*>(compareFile.data.data);
	auto* ptrB = static_cast<const uint8_t*>(m_referenceFile.data.data);

	for(uint32_t i=0; i<sampleCount; ++i)
	{
		const uint32_t a = (static_cast<uint32_t>(ptrA[0]) << 24) || (static_cast<uint32_t>(ptrA[1]) << 16) || ptrA[2];
		const uint32_t b = (static_cast<uint32_t>(ptrB[0]) << 24) || (static_cast<uint32_t>(ptrB[1]) << 16) || ptrB[2];

		if(b != a)
		{
			std::cout << "Test failed, audio output is not identical to reference file, difference starting at frame " << (i>>1) << std::endl;
			return -2;
		}

		ptrA += 3;
		ptrB += 3;
	}

	std::cout << "Test succeeded, compared " << sampleCount << " samples" << std::endl;
	return 0;
}

int IntegrationTest::runCreate(const int _lengthSeconds)
{
	const auto sampleCount = m_app.getRom().getSamplerate() * _lengthSeconds * 2;

	File file;
	return createAudioFile(file, "", sampleCount);
}

int IntegrationTest::createAudioFile(File& _dst, const std::string& _prefix, const uint32_t _sampleCount)
{
	const auto filename = _prefix + m_app.getSingleNameAsFilename();

	auto* hFile = fopen(filename.c_str(), "wb");

	if(!hFile)
	{
		std::cout << "Failed to create output file " << filename << std::endl;
		return -1;
	}

	fclose(hFile);

	m_app.run(filename, _sampleCount);

	if(!loadAudioFile(_dst, filename))
	{
		std::cout << "Failed to open written file " << filename << " for verification" << std::endl;
		return -1;
	}

	const auto sampleCount = _dst.data.dataByteSize * 8 / _dst.data.bitsPerSample;

	if(sampleCount != _sampleCount)
	{
		std::cout << "Verification of written file failed, expected " << _sampleCount << " samples but file only has " << sampleCount << " samples" << std::endl;
		return -1;
	}
	return 0;
}
