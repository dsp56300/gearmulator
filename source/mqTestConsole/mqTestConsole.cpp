#include "../synthLib/os.h"

#include "../mqLib/rom.h"
#include "../mqLib/mqmc.h"

int main(int _argc, char* _argv[])
{
	const auto romFile = synthLib::findROM(524288);

	mqLib::ROM rom(romFile);

	mqLib::MqMc mc(rom);

	while(true)
	{
		mc.exec();
	}
	return 0;
}
