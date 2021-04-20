#include <fstream>
#include <vector>

#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/unittests.h"

#include "../virusLib/accessVirus.h"
#include "../virusLib/syx.h"

#define HEXN(S, n)                     std::hex << std::setfill('0') << std::setw(n) << S

using namespace dsp56k;


std::thread boot_virus_from_file(const AccessVirus& v,DSP& dsp,Peripherals56362& periph)
{
	// Load BootROM in DSP memory
	for (size_t i=0; i<v.bootRom.data.size(); i++)
		dsp.memory().set(dsp56k::MemArea_P, v.bootRom.offset + i, v.bootRom.data[i]);

//	dsp.memory().saveAssembly("Virus_BootROM.asm", v.bootRom.offset, v.bootRom.size, false, false, &periph);

	// Attach command stream
	std::thread feedCommandStream([&]()
	{
		periph.getHDI08().writeRX((int32_t*)&v.commandStream[0],v.commandStream.size());
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

	AccessVirus v(_argv[1]);
	std::thread loader = boot_virus_from_file(v, dsp, periph);

	std::thread dspThread([&]()
	{
		while(true)
		{
			/*
			// Dump memory content at a specific PC
			if(dsp.getPC() == 0x2c183)
			{
				memory.saveAsText("emu_X.txt", MemArea_X, 0, 0x3800);
				memory.saveAsText("emu_Y.txt", MemArea_Y, 0, 0x3800);
				memory.saveAsText("emu_P.txt", MemArea_P, 0, 0x3800);
				memory.saveAsText("emu_E.txt", MemArea_Y, 0x20000, 0x20000);
				memory.saveAssembly("emu_P.asm", 0, 0x3800);
				memory.saveAssembly("emu_E.asm", 0x20000, 0x20000);
			}
			*/
			dsp.exec();
		}
	});

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

			int buf = (word & 0x00ff0000) >> 16;

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
		}
	});


	constexpr size_t sampleCount = 4;
	constexpr size_t channelsIn = 2;
	constexpr size_t channelsOut = 2;

	float inputData[channelsIn][sampleCount] = 
	{
		{1,-1,0.5f,-0.5f},
		{1,-1,0.5f,-0.5f},
	};

	float outputData[channelsOut][sampleCount] = 
	{
		{0,0,0,0},
		{0,0,0,0}
	};

	float* audioIn [channelsIn ] = {inputData[0], inputData[1]};
	float* audioOut[channelsOut] = {outputData[0], outputData[1]};

	// queue for HDI08
	loader.join();
//	memory.saveAssembly("Virus_P.asm", 0, memory.size(), true, false, &periph);

	FILE* hFile = nullptr;
	int ctr=0,go=0;

	v.loadPreset(0, 94);
	
	// Load preset
	Syx syx(periph.getHDI08());
	std::thread sendSyxThread([&]() {
		syx.sendControlCommand(Syx::UNK1a, 0x1);
		syx.sendControlCommand(Syx::UNK1b, 0x1);
		syx.sendControlCommand(Syx::UNK1c, 0x0);
		syx.sendControlCommand(Syx::UNK1d, 0x0);
		syx.sendControlCommand(Syx::UNK35, 0x40);
		syx.sendControlCommand(Syx::UNK36, 0xc);
		syx.sendControlCommand(Syx::UNK36, 0xc); // duplicate
		syx.sendControlCommand(Syx::SECOND_OUTPUT_SELECT, 0x0);
		syx.sendControlCommand(Syx::UNK76, 0x0);
		syx.sendControlCommand(Syx::INPUT_THRU_LEVEL, 0x0);
		syx.sendControlCommand(Syx::INPUT_BOOST, 0x0);
		syx.sendControlCommand(Syx::MASTER_TUNE, 0x40); // issue
		syx.sendControlCommand(Syx::DEVICE_ID, 0x0);
		syx.sendControlCommand(Syx::MIDI_CONTROL_LOW_PAGE, 0x1);
		syx.sendControlCommand(Syx::MIDI_CONTROL_HIGH_PAGE, 0x0);
		syx.sendControlCommand(Syx::MIDI_ARPEGGIATOR_SEND, 0x0);
		syx.sendControlCommand(Syx::MIDI_CLOCK_RX, 0x1);
		syx.sendControlCommand(Syx::GLOBAL_CHANNEL, 0x0);
		syx.sendControlCommand(Syx::LED_MODE, 0x2);
		syx.sendControlCommand(Syx::LCD_CONTRAST, 0x40);
		syx.sendControlCommand(Syx::PANEL_DESTINATION, 0x1);
		syx.sendControlCommand(Syx::UNK_6d, 0x6c);
		syx.sendControlCommand(Syx::CC_MASTER_VOLUME, 0x7a); // issue

		// Send preset
		syx.sendFile(v.preset);
		
		std::this_thread::sleep_for(std::chrono::seconds(3));
		LOG("Sending Note On!");
		syx.sendControlCommand(Syx::AUDITION, 0x7f);
//		syx.sendMIDI(0x90,0x30,0x30);	// Note On
		
	});

	while (true)
	{
		ctr++;
		
		if((ctr & 0xfff) == 0)
		{
			LOG("Deliver Audio");
			dsp.logSC("audio");
		}

		periph.getEsai().processAudioInterleaved(audioIn, audioOut, sampleCount, channelsIn, channelsOut);

		if(!hFile)
		{
			for(auto c=0; c<channelsOut; ++c)
			{
				for(auto i=0; i<sampleCount; ++i)
				{
					if(audioOut[c][i] != 0.0f)
					{
						hFile = fopen("virus_out.raw", "wb");
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
