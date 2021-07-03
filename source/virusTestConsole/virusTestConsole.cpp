#include <fstream>
#include <vector>
#include <cstring>

#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/dspthread.h"
#include "../dsp56300/source/dsp56kEmu/jit.h"
#include "../dsp56300/source/dsp56kEmu/jitunittests.h"
#include "../dsp56300/source/dsp56kEmu/unittests.h"

#include "../virusLib/romfile.h"
#include "../virusLib/syx.h"
#include "../virusLib/midi.h"
#include "../virusLib/wavWriter.h"

using namespace dsp56k;
using namespace virusLib;

std::string audioFilename;
std::vector<uint8_t> audioData;
WavWriter writer;
#if _DEBUG
size_t g_writeBlockSize = 8192;
#else
size_t g_writeBlockSize = 65536;
#endif
size_t g_nextWriteSize = g_writeBlockSize;

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
	Syx* syx=(Syx*)data;

	LOG("Sending Preset!");

	syx->sendControlCommand(Syx::UNK1a, 0x1);
	syx->sendControlCommand(Syx::UNK1b, 0x1);
	syx->sendControlCommand(Syx::UNK1c, 0x0);
	syx->sendControlCommand(Syx::UNK1d, 0x0);
	syx->sendControlCommand(Syx::UNK35, 0x40);
	syx->sendControlCommand(Syx::UNK36, 0xc);
	syx->sendControlCommand(Syx::UNK36, 0xc); // duplicate
	syx->sendControlCommand(Syx::SECOND_OUTPUT_SELECT, 0x0);
	syx->sendControlCommand(Syx::UNK76, 0x0);
	syx->sendControlCommand(Syx::INPUT_THRU_LEVEL, 0x0);
	syx->sendControlCommand(Syx::INPUT_BOOST, 0x0);
	syx->sendControlCommand(Syx::MASTER_TUNE, 0x40); // issue
	syx->sendControlCommand(Syx::DEVICE_ID, 0x0);
	syx->sendControlCommand(Syx::MIDI_CONTROL_LOW_PAGE, 0x1);
	syx->sendControlCommand(Syx::MIDI_CONTROL_HIGH_PAGE, 0x0);
	syx->sendControlCommand(Syx::MIDI_ARPEGGIATOR_SEND, 0x0);
	syx->sendControlCommand(Syx::MIDI_CLOCK_RX, 0x1);
	syx->sendControlCommand(Syx::GLOBAL_CHANNEL, 0x0);
	syx->sendControlCommand(Syx::LED_MODE, 0x2);
	syx->sendControlCommand(Syx::LCD_CONTRAST, 0x40);
	syx->sendControlCommand(Syx::PANEL_DESTINATION, 0x1);
	syx->sendControlCommand(Syx::UNK_6d, 0x6c);
	syx->sendControlCommand(Syx::CC_MASTER_VOLUME, 0x7a); // issue

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
						const auto response = syx->sendSysex(ev.sysex);
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
		syx->sendFile(Syx::SINGLE, syx->getROMFile().preset);
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
	constexpr TWord g_memorySize = 0x040000;	// 128k words beginning at 0x200000
	const DefaultMemoryValidator memoryMap;
	Memory memory(memoryMap, g_memorySize);
	memory.setExternalMemory(0x020000, true);
	Peripherals56362 periph;
	DSP dsp(memory, &periph, &periph);

	periph.getEsai().setCallback(audioCallback,4,1);
	periph.getEsai().writeEmptyAudioIn(4, 2);

	ROMFile v(_argv[1]);
	auto loader = v.bootDSP(dsp, periph);

	for(auto b=0; b<8; ++b)
	{
		for(auto p=0; p<128; ++p)
			v.loadPreset(b, p);
	}

	if(_argc > 2)
	{
		int preset = atoi(_argv[2]);
		const int bank = preset / 128;
		preset -= bank * 128;
		audioFilename = v.loadPreset(bank, preset);
	}
	else
	{
//		audioFilename = v.loadPreset(3, 0x65);	// SmoothBsBC
//		audioFilename = v.loadPreset(0, 12);    // CommerseSV
//		audioFilename = v.loadPreset(0, 23);	// Digedi_JS
//		audioFilename = v.loadPreset(0, 69);	// Mystique
//		audioFilename = v.loadPreset(1, 4);		// Backing
//		audioFilename = v.loadPreset(0, 50);	// Hoppin' SV
//		audioFilename = v.loadPreset(0, 28);	// Etheral SV
//		audioFilename = v.loadPreset(1, 75);	// Oscar1 HS
//		audioFilename = v.loadPreset(0, 93);	// RepeaterJS
//		audioFilename = v.loadPreset(0,126);
//		audioFilename = v.loadPreset(3,101);
//		audioFilename = v.loadPreset(0,5);
//		audioFilename = v.loadPreset(3, 56);	// Impact  MS
//		audioFilename = v.loadPreset(3, 73);	// NiceArp JS
		audioFilename = v.loadPreset(0, 51);	// IndiArp BC
//		audioFilename = v.loadPreset(0, 103);	// SilkArp SV
//		audioFilename = v.loadPreset(3, 15);	// BellaArpJS
//		audioFilename = v.loadPreset(3, 35);	// EnglArp JS
//		audioFilename = v.loadPreset(3, 93);	// Rhy-Arp JS
//		audioFilename = v.loadPreset(3, 119);	// WalkaArpJS
//		audioFilename = v.loadPreset(0, 126);	// Init
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

	std::thread midiThread([&]() {
		int midi[128];
		auto sequenceStarted = false;
		size_t idx = 0;
		while (true) {
			// Only support for single byte responses atm
			uint32_t word = periph.getHDI08().readTX();
			if ((word & 0xff00ffff) != 0) {
				LOG("Unexpected MIDI data: 0x" << HEX(word));
				continue;
			}

			auto buf = (word & 0x00ff0000) >> 16;

			// Check for sequence start 0xf0
			if (!sequenceStarted) {
				if (buf == 0 || buf == 0xf5)
					continue;
				if (buf != 0xf0) {
					LOG("Unexpected MIDI bytes: 0x" << HEXN(buf, 2));
					continue;
				}
				sequenceStarted = true;
			}

			midi[idx] = buf;
			++idx;

			// End of midi command, show it
			if (buf == 0xf7) {
				std::ostringstream stringStream;
				for (size_t i = 0; i < idx; i++) {
					//printf("tmp: 0x%x\n", midi[i]);
					stringStream << HEXN(midi[i], 2);
				}
				LOG("SYSEX RESPONSE: 0x" << stringStream.str());
				memset(midi, 0, sizeof midi);
				sequenceStarted = false;
				idx = 0;
			}

			//LOG("BUF=0x"<< HEX(buf));

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
