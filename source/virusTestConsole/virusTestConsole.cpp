#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/unittests.h"

#include "../virusLib/accessVirus.h"

using namespace dsp56k;

std::thread boot_virus_from_file(const AccessVirus& v,DSP& dsp,Peripherals56362& periph)
{
	// Load BootROM in DSP memory
	for (int i=0; i<v.bootRom.data.size(); i++)
		dsp.memory().set(dsp56k::MemArea_P, v.bootRom.offset + i, v.bootRom.data[i]);
	// Attach command stream
	std::thread feedCommandStream([&]()
	{
		periph.getHDI08().write((int32_t*)&v.commandStream[0],v.commandStream.size());
	});

	// Initialize the DSP
	dsp.setPC(v.bootRom.offset);
	return feedCommandStream;
}


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

	const AccessVirus v(_argv[1]);
	std::thread loader = boot_virus_from_file(v, dsp, periph);

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

	float inputData[channelsIn][sampleCount] = 
	{
		{1,-1,0.5f,-0.5f},
		{1,-1,0.5f,-0.5f},
	};

	float outputData[channelsOut][sampleCount] = 
	{
		{0,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		{0,0,0,0},
		{0,0,0,0}
	};

	float* audioIn [channelsIn ] = {inputData[0], inputData[1]};
	float* audioOut[channelsOut] = {outputData[0], outputData[1], outputData[2], outputData[3], outputData[4], outputData[5]};

	// Make a dummy setup
	int pots[22]={0x1a,0x1b,0x1c,0x1d,0x35,0x36,0x2d,0x4c,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,0x60,0x6a,0x7c,0x7d,0x7e,0x79,0x6d,0x7f};
	int vals[46];
	for (int i=0;i<22;i++) {vals[i*2]=0x72f4f4;vals[i*2+1]=(pots[i]<<8)|0x400000;}
	vals[44]=0x72f4f4;vals[45]=0x7f7f00;
	// queue for HDI08
	loader.join();
	periph.getHDI08().write(vals,46);

	FILE* hFile = nullptr;

	while(true)
	{
		LOG("Deliver Audio");
		periph.getEsai().processAudioInterleaved(audioIn, audioOut, sampleCount, channelsIn, channelsOut);

		if(!hFile)
		{
			for(auto c=0; c<channelsOut; ++c)
			{
				for(auto i=0; i<sampleCount; ++i)
				{
					if(audioOut[c][i] != 0.0f)
					{
//						hFile = fopen("virus_out.raw", "wb");
					}
				}
			}
		}

		if(hFile)
		{
			for(auto i=0; i<sampleCount; ++i)
			{
				for(auto c=0; c<2; ++c)
					fwrite(&audioOut[c][i], sizeof(float), 1, hFile);
			}
		}
	}

	return 0;
}
