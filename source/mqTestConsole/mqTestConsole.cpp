#include "../synthLib/os.h"

#include "../mqLib/rom.h"

int main(int _argc, char* _argv[])
{
	const auto romFile = synthLib::findROM(524288);

	mqLib::ROM rom(romFile);

	return 0;
}
