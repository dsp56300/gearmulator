#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/unittests.h"

#include "../virusLib/romLoader.h"

using namespace dsp56k;

int main(int _argc, char* _argv[])
{
	UnitTests tests;

	constexpr TWord g_memorySize = 0x400000;	// 128k words beginning at 0x200000

	const DefaultMemoryMap memoryMap;
	Memory memory(memoryMap, g_memorySize);

	memory.setExternalMemory(0x200000, true);	// @steven was the starting address 0x200000?

	Peripherals56362 periph;

	DSP dsp(memory, &periph, &periph);

	virusLib::RomLoader romLoader;
	if(!romLoader.loadFromFile(memory, _argv[1]))
		return -1;
	
	std::thread dspThread([&]()
	{
		dsp.setPC(0x000da6);

		while(true)
			dsp.exec();
	});

	constexpr size_t sampleCount = 4;

	float inputDataL[sampleCount] = {1,-1,0.5f,-0.5f};
	float inputDataR[sampleCount] = {1,-1,0.5f,-0.5f};
	float outputDataL[sampleCount] = {0,0,0,0};
	float outputDataR[sampleCount] = {0,0,0,0};

	float* audioIn[2] = {inputDataL, inputDataR};
	float* audioOut[6] = {outputDataL, outputDataR, outputDataL, outputDataR, outputDataL, outputDataR};

	while(true)
	{
		periph.getEsai().processAudioInterleaved(audioIn, audioOut, sampleCount, 2, 6);
	}

	return 0;
}
