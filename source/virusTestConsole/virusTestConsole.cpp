#include <iostream>
#include <vector>

#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/dspthread.h"
#include "../dsp56300/source/dsp56kEmu/jitunittests.h"
#include "../dsp56300/source/dsp56kEmu/unittests.h"

#include "../synthLib/wavWriter.h"
#include "../synthLib/os.h"

#include "../virusLib/romfile.h"
#include "../virusLib/microcontroller.h"
#include "../virusLib/midiOutParser.h"

using namespace dsp56k;
using namespace virusLib;
using namespace synthLib;

std::vector<uint8_t> audioData;
std::string audioFilename;

WavWriter writer;
#if _DEBUG
size_t g_writeBlockSize = 8192;
#else
size_t g_writeBlockSize = 65536;
#endif
size_t g_nextWriteSize = g_writeBlockSize;

Microcontroller::TPreset preset;

void writeWord(const TWord _word)
{
	const auto d = reinterpret_cast<const uint8_t*>(&_word);
	audioData.push_back(d[0]);
	audioData.push_back(d[1]);
	audioData.push_back(d[2]);
}

void audioCallback(dsp56k::Audio* audio)
{
	static FILE *hFile=0;
	static int ctr=0;
	constexpr size_t sampleCount = 4;
	constexpr size_t channelsIn = 2;
	constexpr size_t channelsOut = 2;

	TWord inputData[channelsIn][sampleCount] =	{{0,0,0,0}, {0,0,0,0}};
	TWord* audioIn [channelsIn ] = {inputData[0],  inputData[1] };
	TWord outputData[channelsOut][sampleCount] ={{0, 0,   0,    0},	{0, 0,   0,    0}};
	TWord* audioOut[channelsOut] = {outputData[0], outputData[1]};

	
	ctr++;
	if((ctr & 0xfff) == 0) {LOG("Deliver Audio");}

	audio->processAudioInterleaved(audioIn, audioOut, sampleCount, channelsIn, channelsOut);

	if(!audioData.capacity())
	{
		for(int c=0; c<channelsOut; ++c)
		{
			for(int i=0; i<sampleCount; ++i)
			{
				if(audioOut[c][i] != 0.0f)
				{
					audioData.reserve(2048);
				}
			}
		}

		if(audioData.capacity())
		{
			audioFilename = ROMFile::getSingleName(preset);

			for(size_t i=0; i<audioFilename.size(); ++i)
			{
				if(audioFilename[i] == ' ')
					audioFilename[i] = '_';
			}
			audioFilename = "virusEmu_" + audioFilename + ".wav";
		}
	}

	if(audioData.capacity())
	{
		for(int i=0; i<sampleCount; ++i)
		{
			for(int c=0; c<2; ++c)
				writeWord(audioOut[c][i]);
		}

		if(audioData.size() >= g_nextWriteSize)
		{
			if(writer.write(audioFilename, 24, false, 2, 12000000/256, audioData))
			{
				audioData.clear();
				g_nextWriteSize = g_writeBlockSize;
			}
			else
				g_nextWriteSize += g_writeBlockSize;
		}
	}
}

void loadSingle(ROMFile& r, int b, int p)
{
	r.getSingle(b, p, preset);
}

bool loadSingle(ROMFile& r, const std::string& _preset)
{
	auto isDigit = true;
	for(size_t i=0; i<_preset.size(); ++i)
	{
		if(!isdigit(_preset[i]))
		{
			isDigit = false;
			break;
		}
	}

	if(isDigit)
	{
		int preset = atoi(_preset.c_str());
		const int bank = preset / 128;
		preset -= bank * 128;
		loadSingle(r, bank, preset);
		return true;
	}

	for(uint32_t b=0; b<8; ++b)
	{
		for(uint32_t p=0; p<128; ++p)
		{
			std::array<uint8_t, 256> data;
			r.getSingle(b, p, data);

			const std::string name = ROMFile::getSingleName(data);
			if(name.empty())
			{
				return false;				
			}
			if(name == _preset)
			{
				loadSingle(r, b, p);
				return true;
			}
		}
	}
	return false;
}
void midiNoteOn(void *data,DSP *dsp)
{
	auto* uc = static_cast<Microcontroller*>(data);
	LOG("Sending Note On!");
	uc->sendMIDI(0x90,60,0x7f);	// Note On

}
void midiCallback(void *data,DSP *dsp)
{
	auto* syx = static_cast<Microcontroller*>(data);

	LOG("Sending Preset!");

	syx->sendInitControlCommands();

	// Send preset
	syx->writeSingle(0, Microcontroller::SINGLE, preset, false);

	dsp->setCallback(midiNoteOn, data, 477263+70000*10);
}

int main(int _argc, char* _argv[])
{
	if(true)
	{
		puts("Running Unit Tests...");
//		UnitTests tests;
		JitUnittests jitTests;
//		return 0;
		puts("Unit Tests finished.");
	}

	// Create the DSP with peripherals
	constexpr TWord g_memorySize = 0x100000;	// 128k words beginning at 0x20000
	const DefaultMemoryValidator memoryMap;
	Memory memory(memoryMap, g_memorySize);
	memory.setExternalMemory(0x020000, true);
	Peripherals56362 periph;
	DSP dsp(memory, &periph, &periph);

	periph.getEsai().setCallback(audioCallback,4,1);
	periph.getEsai().writeEmptyAudioIn(4, 2);

	const auto romFile = findROM(1024 * 1024);
	if(romFile.empty())
	{
		std::cout << "Unable to find ROM. Place a ROM file with .bin extension next to this program." << std::endl;
		return -1;
	}
	ROMFile v(romFile);
	auto loader = v.bootDSP(dsp, periph);

	if(_argc > 1)
	{
		if(!loadSingle(v, _argv[1]))
		{
			std::cout << "Failed to find preset '" << _argv[1] << "', make sure to use a ROM that contains it" << std::endl;
			return -1;
		}
	}
	else
	{
//		loadSingle(v, 3, 0x65);		// SmoothBsBC
//		loadSingle(v, 0, 12);		// CommerseSV
//		loadSingle(v, 0, 23);		// Digedi_JS
//		loadSingle(v, 0, 69);		// Mystique
//		loadSingle(v, 1, 4);		// Backing
//		loadSingle(v, 0, 50);		// Hoppin' SV
//		loadSingle(v, 0, 28);		// Etheral SV
//		loadSingle(v, 1, 75);		// Oscar1 HS
//		loadSingle(v, 0, 93);		// RepeaterJS
//		loadSingle(v, 0,126);
//		loadSingle(v, 3,101);
//		loadSingle(v, 0,5);
//		loadSingle(v, 3, 56);		// Impact  MS
//		loadSingle(v, 3, 73);		// NiceArp JS
		loadSingle(v, 0, 51);		// IndiArp BC
//		loadSingle(v, 0, 103);		// SilkArp SV
//		loadSingle(v, 3, 15);		// BellaArpJS
//		loadSingle(v, 3, 35);		// EnglArp JS
//		loadSingle(v, 3, 93);		// Rhy-Arp JS
//		loadSingle(v, 3, 119);		// WalkaArpJS
//		loadSingle(v, 0, 126);		// Init
	}
	// Load preset

	Microcontroller syx(periph.getHDI08(), v);
//	dsp.setCallback(midiCallback, &syx, 477263+70000*5);

	dsp.enableTrace((DSP::TraceMode)(DSP::Ops | DSP::Regs | DSP::StackIndent));

	DSPThread dspThread(dsp);

	MidiOutParser midiOut;
	
	std::thread midiThread([&]() 
	{
		while(true)
		{
			const auto word = periph.getHDI08().readTX();
			midiOut.append(word);
		}
	});

	// queue for HDI08
	loader.join();

	// dump memory to files
//	memory.saveAsText("Virus_X.txt", MemArea_X, 0, memory.size());
//	memory.saveAsText("Virus_Y.txt", MemArea_Y, 0, memory.size());
//	memory.saveAssembly("Virus_P.asm", 0, memory.size(), true, false, &periph);

	while (true)
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}

	return 0;
}
