#include <memory>

#include "../synthLib/os.h"

#include "../mqLib/rom.h"
#include "../mqLib/mqmc.h"

int main(int _argc, char* _argv[])
{
	const auto romFile = synthLib::findROM(524288);

	mqLib::ROM rom(romFile);

	std::unique_ptr<mqLib::MqMc> mc;
	mc.reset(new mqLib::MqMc(rom));

	while(true)
	{
		mc->exec();
	}
	return 0;
}
