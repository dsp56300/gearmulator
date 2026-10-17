#include <iostream>

#include "baseLib/commandline.h"

#include "changelogLib/changelog.h"

int main(const int _argc, char* _argv[])
{
	const baseLib::CommandLine cmdLine(_argc, _argv);

	const auto inFile = cmdLine.get("i");

	if (inFile.empty())
	{
		std::cout << "No input file specified" << '\n';
		return -1;
	}

	auto outPath = cmdLine.get("o");

	if (outPath.empty())
	{
		std::cout << "No output path specified" << '\n';
		return -1;
	}

	if (outPath.back() != '/' && outPath.back() != '\\')
		outPath.push_back('/');

	auto f = cmdLine.get("f");

	auto format = changelogLib::Changelog::Format::Txt;

	if (!f.empty())
	{
		if (f == "discord")
		{
			format = changelogLib::Changelog::Format::Discord;
		}
		else
		{
			std::cout << "Unknown format '" << f << "'\n";
			return -1;
		}
	}

	changelogLib::Changelog changelog;
	std::string error;

	if (!changelog.load(inFile, error) || !changelog.writeFiles(outPath, format, error))
	{
		std::cout << error << '\n';
		return -1;
	}

	return 0;
}
