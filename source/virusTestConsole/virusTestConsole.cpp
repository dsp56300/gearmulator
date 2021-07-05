#include <fstream>
#include <vector>

#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/dspthread.h"
#include "../dsp56300/source/dsp56kEmu/jitunittests.h"
#include "../dsp56300/source/dsp56kEmu/unittests.h"

#include "../virusLib/romfile.h"
#include "../virusLib/syx.h"
#include "../virusLib/midi.h"
#include "../virusLib/wavWriter.h"
#include "../virusLib/midiOutParser.h"

using namespace dsp56k;
using namespace virusLib;

std::vector<uint8_t> audioData;
std::string audioFilename;

WavWriter writer;
#if _DEBUG
size_t g_writeBlockSize = 8192;
#else
size_t g_writeBlockSize = 65536;
#endif
size_t g_nextWriteSize = g_writeBlockSize;

Syx::TPreset preset;

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
//					memory.clearHeatmap();
//					saveHeatmapInstr = dsp.getInstructionCounter()+0x10000000;
					audioData.reserve(2048);
				}
			}
		}

		if(audioData.capacity())
		{
			audioFilename = Syx::getPresetName(preset);

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

bool midiMode = false;
void midiNoteOn(void *data,DSP *dsp)
{
	Syx* syx=(Syx*)data;
	LOG("Sending Note On!");
	syx->sendMIDI(0x90,60,0x7f);	// Note On
//		syx->sendControlCommand(Syx::AUDITION, 0x7f);
//		syx->sendMIDI(0xB0,113,0);	// Send FX off

}
void midiCallback(void *data,DSP *dsp)
{
	auto* syx = static_cast<Syx*>(data);

	LOG("Sending Preset!");

	syx->sendInitControlCommands();

	if (midiMode)
	{
		std::thread sendSyxThread([&]() {
			Midi midi;
			if (midi.connect() == 0)
			{
				
				SMidiEvent ev;
				while (true)
				{
					if (midi.read(ev) == Midi::midiNoData)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(200));
						continue;
					}

					if (!ev.sysex.empty())
					{
						std::vector<unsigned char> response;
						syx->sendSysex(ev.sysex, false, response);
						if (!response.empty())
						{
							SMidiEvent out;
							out.sysex = response;
							midi.write(out);
						}
					}
					else
					{
						syx->sendMIDI(ev.a, ev.b, ev.c);
					}

					ev = {};
				}
			}
		});
	}
	else
	{
		// Send preset
		syx->sendSingle(0, Syx::SINGLE, preset, false);
//			syx->send(Syx::Page::PAGE_B,0,100, 1);		// distortion curve. setting this to nonzero will break a preset.

//			syx->send(Syx::Page::PAGE_A,0,49, 0);		// saturation curve.
//			syx->send(Syx::Page::PAGE_A,0,51, 7);		// filter type

		// MIDI Tempo meta message, set tempo to 120 bpm
//			syx->sendMIDI(0xFF,0x51,0x03);
//			syx->sendMIDI(0x07,0xA1,0x20);
		
		dsp->setCallback(midiNoteOn, data, 477263+70000*10);

	}
}

int main(int _argc, char* _argv[])
{
	if(true)
	{
//		UnitTests tests;
		JitUnittests jitTests;
//		return 0;
	}

	// Create the DSP with peripherals
	constexpr TWord g_memorySize = 0x040000;	// 128k words beginning at 0x20000
	const DefaultMemoryValidator memoryMap;
	Memory memory(memoryMap, g_memorySize);
	memory.setExternalMemory(0x020000, true);
	Peripherals56362 periph;
	DSP dsp(memory, &periph, &periph);

	periph.getEsai().setCallback(audioCallback,4,1);
	periph.getEsai().writeEmptyAudioIn(4, 2);

	ROMFile v(_argv[1]);
	auto loader = v.bootDSP(dsp, periph);

	if(_argc > 2)
	{
		int preset = atoi(_argv[2]);
		const int bank = preset / 128;
		preset -= bank * 128;
		loadSingle(v, bank, preset);
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
	midiMode = false;//_argc >= 3;
	Syx syx(periph.getHDI08(), v);
	dsp.setCallback(midiCallback, &syx, 477263+70000*5);

	dsp.enableTrace((DSP::TraceMode)(DSP::Ops | DSP::Regs | DSP::StackIndent));
	long long saveHeatmapInstr = 0;

#if MEMORY_HEAT_MAP
	const auto thread = std::thread([&]
	{
		while(true)
		{
			dsp.exec();

			if(saveHeatmapInstr && dsp.getInstructionCounter() >= saveHeatmapInstr)
			{
				memory.saveHeatmap("heatmap.txt", false);
				saveHeatmapInstr=0;
			}
		}
	});
#else
	DSPThread dspThread(dsp);
#endif

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
