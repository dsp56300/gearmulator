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
	bool loadReferenceFile(const std::string& _filename);
	int runCompare();
	int runCreate(int _lengthSeconds);

	const CommandLine& m_cmd;
	const std::string m_romFile;
	ConsoleApp m_app;

	std::vector<uint8_t> m_referenceFileData;
	synthLib::Data m_referenceData;
};
