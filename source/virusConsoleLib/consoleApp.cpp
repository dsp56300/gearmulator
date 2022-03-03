#include "consoleApp.h"

#include <iostream>

#include "esaiListenerToCallback.h"
#include "esaiListenerToFile.h"
#include "dsp56kEmu/dspthread.h"
#include "dsp56kEmu/memory.h"

#include "../virusLib/midiOutParser.h"

using namespace virusLib;
using namespace synthLib;

class EsaiListener;
const dsp56k::DefaultMemoryValidator g_memoryMap;

ConsoleApp::ConsoleApp(const std::string& _romFile, const uint32_t _memorySize, const uint32_t _extMemAddress)
: memory(g_memoryMap, _memorySize)
, v(_romFile)
, periphX(&periphY)
, dsp(memory, &periphX, &getYPeripherals())
, uc(periphX.getHDI08(), v)
, preset({})
{
	if (!v.isValid())
	{
		std::cout << "ROM file " << _romFile << " is not valid and couldn't be loaded. Place a valid ROM file with .bin extension next to this program." << std::endl;
		return;
	}

	memory.setExternalMemory(_extMemAddress, true);

	auto& jit = dsp.getJit();
	auto conf = jit.getConfig();
	conf.aguSupportBitreverse = v.getModel() == ROMFile::ModelD;	// not used on B & C

	jit.setConfig(conf);
}

bool ConsoleApp::isValid() const
{
	return v.isValid();
}

void ConsoleApp::waitReturn()
{
	std::cin.ignore();
}

std::thread ConsoleApp::bootDSP()
{
	return v.bootDSP(dsp, periphX);
}

dsp56k::IPeripherals& ConsoleApp::getYPeripherals()
{
	if (v.getModel() == ROMFile::ModelD)
		return periphY;

	return periphNop;
}


void ConsoleApp::loadSingle(int b, int p)
{
	v.getSingle(b, p, preset);
}

bool ConsoleApp::loadSingle(const std::string& _preset)
{
	auto isDigit = true;
	for (size_t i = 0; i < _preset.size(); ++i)
	{
		if (!isdigit(_preset[i]))
		{
			isDigit = false;
			break;
		}
	}

	if (isDigit)
	{
		int preset = atoi(_preset.c_str());
		const int bank = preset / v.getPresetsPerBank();
		preset -= bank * v.getPresetsPerBank();
		loadSingle(bank, preset);
		return true;
	}

	for (uint32_t b = 0; b < 8; ++b)
	{
		for (uint32_t p = 0; p < v.getPresetsPerBank(); ++p)
		{
			Microcontroller::TPreset data;
			v.getSingle(b, p, data);

			const std::string name = ROMFile::getSingleName(data);
			if (name.empty())
			{
				return false;
			}
			if (name == _preset)
			{
				loadSingle(b, p);
				return true;
			}
		}
	}
	return false;
}

std::string ConsoleApp::getSingleName() const
{
	return ROMFile::getSingleName(preset);
}

std::string ConsoleApp::getSingleNameAsFilename() const
{
	auto audioFilename = getSingleName();

	for (size_t i = 0; i < audioFilename.size(); ++i)
	{
		if (audioFilename[i] == ' ')
			audioFilename[i] = '_';
	}
	return "virusEmu_" + audioFilename + ".wav";
}

void ConsoleApp::audioCallback(uint32_t audioCallbackCount)
{
	switch (audioCallbackCount)
	{
	case 1:
		LOG("Sending Init Control Commands");
		uc.sendInitControlCommands();
		break;
	case 256:
		LOG("Sending Preset");
		uc.writeSingle(BankNumber::EditBuffer, SINGLE, preset);
		break;
	case 512:
		LOG("Sending Note On");
//		uc.sendMIDI(SMidiEvent(0x90, 36, 0x5f));	// Note On
//		uc.sendMIDI(SMidiEvent(0x90, 48, 0x5f));	// Note On
		uc.sendMIDI(SMidiEvent(0x90, 60, 0x5f));	// Note On
//		uc.sendMIDI(SMidiEvent(0x90, 63, 0x5f));	// Note On
//		uc.sendMIDI(SMidiEvent(0x90, 67, 0x5f));	// Note On
//		uc.sendMIDI(SMidiEvent(0x90, 72, 0x5f));	// Note On
//		uc.sendMIDI(SMidiEvent(0x90, 75, 0x5f));	// Note On
//		uc.sendMIDI(SMidiEvent(0x90, 79, 0x5f));	// Note On
//		uc.sendMIDI(SMidiEvent(0xb0, 1, 0));		// Modwheel 0
		uc.sendPendingMidiEvents(std::numeric_limits<uint32_t>::max());
		break;
/*	case 8000:
		LOG("Sending 2nd Note On");
		uc.sendMIDI(SMidiEvent(0x90, 67, 0x7f));	// Note On
		uc.sendPendingMidiEvents(std::numeric_limits<uint32_t>::max());
		break;
	case 16000:
		LOG("Sending 3rd Note On");
		uc.sendMIDI(SMidiEvent(0x90, 63, 0x7f));	// Note On
		uc.sendPendingMidiEvents(std::numeric_limits<uint32_t>::max());
		break;
*/
	}
}

void ConsoleApp::run(const std::string& _audioOutputFilename, EsaiListenerToCallback::TDataCallback _callback, uint32_t _maxSampleCount/* = 0*/)
{
	auto loader = bootDSP();

//	dsp.enableTrace((DSP::TraceMode)(DSP::Ops | DSP::Regs | DSP::StackIndent));

	const auto sr = v.getSamplerate();

	std::unique_ptr<EsaiListener> esaiListener;

	if (!_audioOutputFilename.empty())
		esaiListener.reset(new EsaiListenerToFile(periphX.getEsai(), 0b001, [&](EsaiListener*, uint32_t _count) { audioCallback(_count); }, sr, _audioOutputFilename));
	else
		esaiListener.reset(new EsaiListenerToCallback(periphX.getEsai(), 0b001, [&](EsaiListener*, uint32_t _count) { audioCallback(_count); }, std::move(_callback)));

	esaiListener->setMaxSamplecount(_maxSampleCount);

	EsaiListenerToFile esaiListener1(periphY.getEsai(), 0b100, [](EsaiListener*, uint32_t) {}, sr, "");

	dsp56k::DSPThread dspThread(dsp);

	MidiOutParser midiOut;

	std::thread midiThread([&]()
	{
		while (!esaiListener->limitReached())
		{
			if(periphX.getHDI08().hasTX())
			{
				const auto word = periphX.getHDI08().readTX();
				midiOut.append(word);
			}
			else
			{
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
		}
	});

	loader.join();

	while (!esaiListener->limitReached())
		std::this_thread::sleep_for(std::chrono::seconds(1));

	midiThread.join();
	dspThread.join();
}

void ConsoleApp::run(const std::string& _audioOutputFilename, uint32_t _maxSampleCount/* = 0*/)
{
	assert(!_audioOutputFilename.empty());
	run(_audioOutputFilename, [](const std::vector<dsp56k::TWord>&) {return true; }, _maxSampleCount);
}

void ConsoleApp::run(EsaiListenerToCallback::TDataCallback _callback, uint32_t _maxSampleCount/* = 0*/)
{
	run(std::string(), std::move(_callback), _maxSampleCount);
}
