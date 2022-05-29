#include <memory>

#include "../mqLib/mqdsp.h"
#include "../synthLib/os.h"

#include "../mqLib/rom.h"
#include "../mqLib/mqmc.h"
#include "dsp56kEmu/dspthread.h"

namespace mqLib
{
	class MqDsp;
}

#define DSP 1

int main(int _argc, char* _argv[])
{
	const auto romFile = synthLib::findROM(524288);

	mqLib::ROM rom(romFile);

	std::unique_ptr<mqLib::MqMc> mc;
	std::unique_ptr<mqLib::MqDsp> dsp;

	mc.reset(new mqLib::MqMc(rom));
	dsp.reset(new mqLib::MqDsp());

	std::deque<uint32_t> txData;

#if DSP
	dsp56k::DSPThread dspThread(dsp->dsp());
	bool dumpDSP = false;
#endif
	while(true)
	{
		mc->exec();

#if DSP
		mc->hdi08().pollTx(txData);
		for (const uint32_t data : txData)
			dsp->hdi08().writeRX(&data, 1);

		uint8_t interruptAddr;
		if(mc->hdi08().pollInterruptRequest(interruptAddr))
			dsp->dsp().injectInterrupt(interruptAddr);

		while(dsp->hdi08().hasTX())
		{
			mc->hdi08().writeRx(dsp->hdi08().readTX());
		}

		if(dumpDSP)
		{
			dsp->dsp().coreDump();
			dumpDSP = false;
		}
#endif
		txData.clear();
	}

	return 0;
}
