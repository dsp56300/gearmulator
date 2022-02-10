#include <iostream>
#include <vector>

#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/dspthread.h"
#include "../dsp56300/source/dsp56kEmu/jitunittests.h"
#include "../dsp56300/source/dsp56kEmu/interpreterunittests.h"

#include "../synthLib/os.h"

#include "../virusLib/romfile.h"
#include "../virusLib/microcontroller.h"
#include "../virusLib/midiOutParser.h"

#include "esaiListener.h"

using namespace dsp56k;
using namespace virusLib;
using namespace synthLib;

Microcontroller::TPreset preset;

Microcontroller* microcontroller = nullptr;

void audioCallback(EsaiListener*, uint32_t audioCallbackCount)
{
	switch (audioCallbackCount)
	{
	case 1:
		LOG("Sending Init Control Commands");
		microcontroller->sendInitControlCommands();
		break;
	case 256:
		LOG("Sending Preset");
		microcontroller->writeSingle(BankNumber::EditBuffer, SINGLE, preset);
		break;
	case 512:
		LOG("Sending Note On");
//		microcontroller->sendMIDI(SMidiEvent(0x90, 36, 0x5f));	// Note On
//		microcontroller->sendMIDI(SMidiEvent(0x90, 48, 0x5f));	// Note On
		microcontroller->sendMIDI(SMidiEvent(0x90, 60, 0x5f));	// Note On
//		microcontroller->sendMIDI(SMidiEvent(0x90, 63, 0x5f));	// Note On
//		microcontroller->sendMIDI(SMidiEvent(0x90, 67, 0x5f));	// Note On
//		microcontroller->sendMIDI(SMidiEvent(0x90, 72, 0x5f));	// Note On
//		microcontroller->sendMIDI(SMidiEvent(0x90, 75, 0x5f));	// Note On
//		microcontroller->sendMIDI(SMidiEvent(0x90, 79, 0x5f));	// Note On
//		microcontroller->sendMIDI(SMidiEvent(0xb0, 1, 0));	// Modwheel 0
		microcontroller->sendPendingMidiEvents(std::numeric_limits<uint32_t>::max());
		break;
/*	case 8000:
		LOG("Sending 2nd Note On");
		microcontroller->sendMIDI(SMidiEvent(0x90, 67, 0x7f));	// Note On
		microcontroller->sendPendingMidiEvents(std::numeric_limits<uint32_t>::max());
		break;
	case 16000:
		LOG("Sending 3rd Note On");
		microcontroller->sendMIDI(SMidiEvent(0x90, 63, 0x7f));	// Note On
		microcontroller->sendPendingMidiEvents(std::numeric_limits<uint32_t>::max());
		break;
*/
	}
}

void loadSingle(const ROMFile& r, int b, int p)
{
	r.getSingle(b, p, preset);
}

bool loadSingle(const ROMFile& r, const std::string& _preset)
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
			Microcontroller::TPreset data;
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

auto waitReturn = []()
{
	std::cin.ignore();
};

int main(int _argc, char* _argv[])
{
	if(true)
	{
		try
		{
			puts("Running Unit Tests...");
//			InterpreterUnitTests tests;
			JitUnittests jitTests;
			puts("Unit Tests finished.");
		}
		catch(const std::string& _err)
		{
			std::cout << "Unit test failed: " << _err << std::endl;
			waitReturn();
			return -1;
		}
	}

	// Create the DSP with peripherals
	constexpr TWord g_memorySize = 0x100000;	// 128k words beginning at 0x20000
	const DefaultMemoryValidator memoryMap;
	Memory memory(memoryMap, g_memorySize);
	memory.setExternalMemory(0x020000, true);

	const auto romFile = findROM(ROMFile::getRomSizeModelD());
	if(romFile.empty())
	{
		std::cout << "Unable to find ROM. Place a ROM file with .bin extension next to this program." << std::endl;
		waitReturn();
		return -1;
	}
	ROMFile v(romFile);

	Peripherals56367 periphY;
	Peripherals56362 periphX(&periphY);
	PeripheralsNop periphNop;

	IPeripherals* pY = &periphNop;

	if (v.getModel() == ROMFile::ModelD)
		pY = &periphY;

	DSP dsp(memory, &periphX, pY);

	auto loader = v.bootDSP(dsp, periphX);

	if(_argc > 1)
	{
		if(!loadSingle(v, _argv[1]))
		{
			std::cout << "Failed to find preset '" << _argv[1] << "', make sure to use a ROM that contains it" << std::endl;
			waitReturn();
			return -1;
		}
	}
	else
	{
//		loadSingle(v, 3, 56);		// Impact  MS
		loadSingle(v, 0, 51);		// IndiArp BC
	}

	Microcontroller uc(periphX.getHDI08(), v);
	microcontroller = &uc;

//	dsp.enableTrace((DSP::TraceMode)(DSP::Ops | DSP::Regs | DSP::StackIndent));

	std::string audioFilename = ROMFile::getSingleName(preset);

	for (size_t i = 0; i < audioFilename.size(); ++i)
	{
		if (audioFilename[i] == ' ')
			audioFilename[i] = '_';
	}
	audioFilename = "virusEmu_" + audioFilename + ".wav";

	const auto sr = v.getModel() == ROMFile::ModelD ? 44100 : 12000000 / 256;

	EsaiListener esaiListener(periphX.getEsai(), audioFilename, 0b001, audioCallback, sr);
	EsaiListener esaiListener1(periphY.getEsai(), "virusEmu_ESAI_1.wav", 0b100, [](EsaiListener*, uint32_t){}, sr);

	DSPThread dspThread(dsp);

	MidiOutParser midiOut;
	
	std::thread midiThread([&]() 
	{
		while(true)
		{
			const auto word = periphX.getHDI08().readTX();
			midiOut.append(word);
		}
	});

	// queue for HDI08
	loader.join();
//	microcontroller->sendInitControlCommands();

	// dump memory to files
//	memory.saveAsText((romFile + "_X.txt").c_str(), MemArea_X, 0, memory.size());
//	memory.saveAsText((romFile + "_Y.txt").c_str(), MemArea_Y, 0, memory.size());
//	memory.saveAssembly((romFile + "_P.asm").c_str(), 0, memory.size(), true, false, &periph);

	while (true)
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}

	return 0;
}
