#include "jucePluginLibTests.h"
#include "baseLib/os.h"

#include <iostream>

int main(int argc, char* argv[])
{
	baseLib::disableErrorDialogs();

	if (const auto result = mcpDiscoveryChild(argc, argv); result >= 0)
		return result;

	try
	{
		testMcpDiscovery();
		testMidiNotifier();
		testParameterDescriptions();
		testPatchDb();
		testPatchManager();
		testSkinVariables();

		std::cout << std::endl;
		std::cout << "All tests passed successfully!" << std::endl;
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "Test failed with exception: " << e.what() << std::endl;
		return 1;
	}
	catch (...)
	{
		std::cerr << "Test failed with unknown exception" << std::endl;
		return 1;
	}
}
