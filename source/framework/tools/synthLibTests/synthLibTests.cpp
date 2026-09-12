#include "synthLibTests.h"

#include <iostream>

int main()
{
	try
	{
		testFilesystem();
		testMidiToSysex();
		testShortMessage();
		testUniversalTuning();

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
