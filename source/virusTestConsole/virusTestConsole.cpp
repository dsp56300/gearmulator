#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/unittests.h"

#include "../virusLib/accessVirus.h"

using namespace dsp56k;

int main(int _argc, char* _argv[])
{
	UnitTests tests;

	// Create the DSP with peripherals
	constexpr TWord g_memorySize = 0x040000;	// 128k words beginning at 0x200000
	const DefaultMemoryMap memoryMap;
	Memory memory(memoryMap, g_memorySize);
	memory.setExternalMemory(0x020000, true);
	Peripherals56362 periph;
	DSP dsp(memory, &periph, &periph);

	// Get the flash contents
	const AccessVirus v(_argv[1]);
	const auto rom = v.get_dsp_program();
	const auto& bootRom = rom.bootRom;
	
	printf("Program BootROM size = 0x%x\n", bootRom.size);
	printf("Program BootROM offset = 0x%x\n", bootRom.offset);
	printf("Program BootROM len = 0x%lx\n", bootRom.data.size());
	printf("Program Commands len = 0x%lx\n", rom.commandStream.size());

	// Load BootROM in DSP memory
	for (int i=0; i<bootRom.data.size(); i++)
		dsp.memory().set(dsp56k::MemArea_P, bootRom.offset + i, bootRom.data[i]);
	// Attach command stream
	periph.getHDI08().writeCommandStream(rom.commandStream);
	// Initialize the DSP
	dsp.setPC(bootRom.offset);
	// And run until we get rebooted!
	while (!periph.readAndClearResetState())
		dsp.exec();

//	memory.saveAssembly("Virus_P.asm", 0, g_memorySize, false, true);

	dsp.enableTrace(true);

	std::thread dspThread([&]()
	{
		while(true)
		{
			dsp.exec();
		}
	});

	constexpr size_t sampleCount = 4;
	constexpr size_t channelsIn = 2;
	constexpr size_t channelsOut = 6;

	float inputDataL[sampleCount] = {1,-1,0.5f,-0.5f};
	float inputDataR[sampleCount] = {1,-1,0.5f,-0.5f};
	float outputDataL[sampleCount] = {0,0,0,0};
	float outputDataR[sampleCount] = {0,0,0,0};

	float* audioIn [channelsIn ] = {inputDataL, inputDataR};
	float* audioOut[channelsOut] = {outputDataL, outputDataR, outputDataL, outputDataR, outputDataL, outputDataR};

	// Make a dummy setup
	int pots[22]={0x1a,0x1b,0x1c,0x1d,0x35,0x36,0x2d,0x4c,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,0x60,0x6a,0x7c,0x7d,0x7e,0x79,0x6d,0x7f};
	int vals[46];
	for (int i=0;i<22;i++) {vals[i*2]=0x72f4f4;vals[i*2+1]=(pots[i]<<8)|0x400000;}
	vals[44]=0x72f4f4;vals[45]=0x7f7f00;
	// queue for HDI08
	periph.getHDI08().write(vals,46);
	
	while(true)
	{
		LOG("Deliver Audio");
		periph.getEsai().processAudioInterleaved(audioIn, audioOut, sampleCount, 2, 6);

		for(auto c=0; c<channelsOut; ++c)
		{
			for(auto i=0; i<sampleCount; ++i)
			{
				assert(audioOut[c][i] == 0.0f && "We've to sample data!");
			}
		}
	}

	return 0;
}
