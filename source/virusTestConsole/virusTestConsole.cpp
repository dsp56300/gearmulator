#include <iostream>

#include "../virusConsoleLib/consoleApp.h"

#include "../dsp56300/source/dsp56kEmu/jitunittests.h"

#include "../synthLib/os.h"

using namespace dsp56k;
using namespace virusLib;
using namespace synthLib;

int main(int _argc, char* _argv[])
{
	if constexpr(true)
	{
		try
		{
			puts("Running Unit Tests...");
//			InterpreterUnitTests tests;
			JitUnittests jitTests;
			puts("Unit Tests finished.");
		}
		catch(const std::string& _err)
		{
			std::cout << "Unit test failed: " << _err << std::endl;
			ConsoleApp::waitReturn();
			return -1;
		}
	}

	const auto romFile = findROM(0);

	if(romFile.empty())
	{
		std::cout << "Unable to find ROM. Place a ROM file with .bin extension next to this program." << std::endl;
		ConsoleApp::waitReturn();
		return -1;
	}

	ConsoleApp app(romFile, 0x100000, 0x020000);

	if(!app.isValid())
	{
		std::cout << "ROM file " << romFile << " couldn't be loaded. Make sure tha the ROM file is valid" << std::endl;
		ConsoleApp::waitReturn();
		return -1;
	}

	if(_argc > 1)
	{
		if(!app.loadSingle(_argv[1]))
		{
			std::cout << "Failed to find preset '" << _argv[1] << "', make sure to use a ROM that contains it" << std::endl;
			ConsoleApp::waitReturn();
			return -1;
		}
	}
	else
	{
//		app.loadSingle(3, 56);		// Impact  MS
		app.loadSingle(0, 51);		// IndiArp BC
	}

	const std::string audioFilename = app.getSingleNameAsFilename();

	app.run(audioFilename);

	return 0;
}
