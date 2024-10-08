#include <iostream>

#include "integrationTest.h"

#include <fstream>
#include <utility>

#include "virusConsoleLib/consoleApp.h"

#include "dsp56kEmu/jitunittests.h"

#include "../dsp56300/source/disassemble/commandline.h"

#include "synthLib/wavReader.h"
#include "synthLib/os.h"

#include "virusLib/romloader.h"

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
		bool forever = true;

		while(forever)
		{
			const CommandLine cmd(_argc, _argv);

			forever = cmd.contains("forever");

			std::vector<std::pair<std::string, std::string>> finishedTests;	// rom, preset
			finishedTests.reserve(64);

			if(cmd.contains("rom") && cmd.contains("preset"))
			{
				const auto romFile = cmd.get("rom");
				const auto preset = cmd.get("preset");

				IntegrationTest test(cmd, romFile, preset, std::string(), virusLib::DeviceModel::Snow);

				const auto res = test.run();
				if(0 == res)
					std::cout << "test successful, ROM " << synthLib::getFilenameWithoutPath(romFile) << ", preset " << preset << '\n';
				return res;
			}
			if(cmd.contains("folder"))
			{
				std::vector<std::string> subfolders;
				synthLib::getDirectoryEntries(subfolders, cmd.get("folder"));

				if(subfolders.empty())
				{
					std::cout << "Nothing found for testing in folder " << cmd.get("folder") << std::endl;
					return -1;
				}

				for (auto& subfolder : subfolders)
				{
					if(subfolder.find("/.") != std::string::npos)
						continue;
					if(subfolder.find('#') != std::string::npos)
						continue;

					std::vector<std::string> files;
					synthLib::getDirectoryEntries(files, subfolder);

					std::string romFile;
					std::string presetsFile;

					if(files.empty())
					{
						std::cout << "Directory " << subfolder << " doesn't contain any files" << std::endl;
						return -1;
					}

					for (auto& file : files)
					{
						if(synthLib::hasExtension(file, ".txt"))
							presetsFile = file;
						else if(synthLib::hasExtension(file, ".bin"))
							romFile = file;
						else if(synthLib::hasExtension(file, ".mid"))
						{
							const auto rom = virusLib::ROMLoader::findROM(file);
							if(rom.isValid())
								romFile = file;
						}
					}

					if(romFile.empty())
					{
						std::cout << "Failed to find ROM in folder " << subfolder << std::endl;
						return -1;
					}
					if(presetsFile.empty())
					{
						std::cout << "Failed to find presets file in folder " << subfolder << std::endl;
						return -1;
					}

					std::vector<std::string> presets;

					std::ifstream ss;
					ss.open(presetsFile.c_str(), std::ios::in);

					if(!ss.is_open())
					{
						std::cout << "Failed to open presets file " << presetsFile << std::endl;
						return -1;
					}

					std::string line;

					while(std::getline(ss, line))
					{
						while(!line.empty() && line.find_last_of("\r\n") != std::string::npos)
							line = line.substr(0, line.size()-1);
						if(!line.empty() && line[0] != '#')
							presets.push_back(line);
					}

					ss.close();

					if(presets.empty())
					{
						std::cout << "Presets file " << presetsFile << "  is empty" << std::endl;
						return -1;
					}

					for (auto& preset : presets)
					{
						IntegrationTest test(cmd, romFile, preset, subfolder + '/', virusLib::DeviceModel::Snow);
						if(test.run() != 0)
							return -1;
						finishedTests.emplace_back(romFile, preset);
					}
				}

				if(!forever)
				{
					std::cout << "All " << finishedTests.size() << " tests finished successfully:" << '\n';
					for (const auto& [rom,preset] : finishedTests)
						std::cout << "ROM " << synthLib::getFilenameWithoutPath(rom) << ", preset " << preset << '\n';
					return 0;
				}
			}
		}
		std::cout << "invalid command line arguments" << std::endl;
		return -1;
	}
	catch(const std::runtime_error& _err)
	{
		std::cout << _err.what() << std::endl;
		return -1;
	}
}

IntegrationTest::IntegrationTest(const CommandLine& _commandLine, std::string _romFile, std::string _presetName, std::string _outputFolder, const virusLib::DeviceModel _tiModel)
	: m_cmd(_commandLine)
	, m_romFile(std::move(_romFile))
	, m_presetName(std::move(_presetName))
	, m_outputFolder(std::move(_outputFolder))
	, m_app(m_romFile, _tiModel)
{
}

int IntegrationTest::run()
{
	if (!m_app.isValid())
	{
		std::cout << "Failed to load ROM " << m_romFile << ", make sure that the ROM file is valid" << std::endl;
		return -1;
	}

	if (!m_app.loadSingle(m_presetName))
	{
		std::cout << "Failed to find preset '" << m_presetName << "', make sure to use a ROM that contains it" << std::endl;
		return -1;
	}

	const int lengthSeconds = m_cmd.contains("length") ? m_cmd.getInt("length") : 0;
	if(lengthSeconds > 0)
	{
		// create reference file
		return runCreate(lengthSeconds);
	}

	const auto referenceFile = m_app.getSingleNameAsFilename();

	if (!loadAudioFile(m_referenceFile, m_outputFolder + referenceFile))
		return -1;

	return runCompare();
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
	const size_t size = ftell(hFile);
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
	const auto frameCount = sampleCount >> 1;

	std::vector<uint8_t> temp;

	File compareFile;
	const auto res = createAudioFile(compareFile, "compare_", static_cast<uint32_t>(frameCount));
	if(res)
		return res;

	auto* ptrA = static_cast<const uint8_t*>(compareFile.data.data);
	auto* ptrB = static_cast<const uint8_t*>(m_referenceFile.data.data);

	for(uint32_t i=0; i<sampleCount; ++i)
	{
		const uint32_t a = (static_cast<uint32_t>(ptrA[0]) << 24) | (static_cast<uint32_t>(ptrA[1]) << 16) | ptrA[2];
		const uint32_t b = (static_cast<uint32_t>(ptrB[0]) << 24) | (static_cast<uint32_t>(ptrB[1]) << 16) | ptrB[2];

		if(b != a)
		{
			std::cout << "Test failed, audio output is not identical to reference file, difference starting at frame " << (i>>1) << ", ROM " << m_romFile << ", preset " << m_presetName << std::endl;
			return -2;
		}

		ptrA += 3;
		ptrB += 3;
	}

	std::cout << "Test succeeded, compared " << sampleCount << " samples, ROM " << m_romFile << ", preset " << m_presetName << std::endl;
	return 0;
}

int IntegrationTest::runCreate(const int _lengthSeconds)
{
	const auto sampleCount = m_app.getRom().getSamplerate() * _lengthSeconds;

	File file;
	return createAudioFile(file, "", sampleCount);
}

int IntegrationTest::createAudioFile(File& _dst, const std::string& _prefix, const uint32_t _sampleCount)
{
	const auto filename = m_outputFolder + _prefix + m_app.getSingleNameAsFilename();

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

	const auto sampleCount = _dst.data.dataByteSize * 8 / _dst.data.bitsPerSample / 2;

	if(sampleCount != _sampleCount)
	{
		std::cout << "Verification of written file failed, expected " << _sampleCount << " samples but file only has " << sampleCount << " samples" << std::endl;
		return -1;
	}
	return 0;
}
