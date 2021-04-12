#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/unittests.h"

#include "../virusLib/romLoader.h"

using namespace dsp56k;

int main(int _argc, char* _argv[])
{
	UnitTests tests;

	constexpr TWord g_memorySize = 0x040000;	// 128k words beginning at 0x200000

	const DefaultMemoryMap memoryMap;
	Memory memory(memoryMap, g_memorySize);

	memory.setExternalMemory(0x020000, true);

	Peripherals56362 periph;

	DSP dsp(memory, &periph, &periph);

	virusLib::RomLoader romLoader;
	if(!romLoader.loadFromFile(dsp, _argv[1]))
		return -1;

//	memory.saveAssembly("Virus_P.asm", 0, g_memorySize, false, true);

	dsp.setPeriph(0, &periph);

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
