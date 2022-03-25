#pragma once

#include "../virusConsoleLib/consoleApp.h"

#include "../synthLib/wavReader.h"

class CommandLine;

class IntegrationTest
{
public:
	explicit IntegrationTest(const CommandLine& _commandLine);

	int run();

private:
	struct File
	{
		std::vector<uint8_t> file;
		synthLib::Data data;
	};

	bool loadAudioFile(File& _dst, const std::string& _filename) const;
	int runCompare();
	int runCreate(int _lengthSeconds);
	int createAudioFile(File& _dst, const std::string& _prefix, uint32_t _sampleCount);

	const CommandLine& m_cmd;
	const std::string m_romFile;
	ConsoleApp m_app;

	File m_referenceFile;
};
