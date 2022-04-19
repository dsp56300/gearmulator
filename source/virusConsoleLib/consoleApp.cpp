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
, m_romName(_romFile)
, v(_romFile, ROMFile::TIModel::TI)
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
	conf.aguSupportBitreverse = v.isTIFamily();	// not used on B & C

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
	if (v.isTIFamily())
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

bool ConsoleApp::loadDemo(const std::string& _filename)
{
	m_demo.reset(new DemoPlayback(uc));
	if(m_demo->loadMidi(_filename))
		return true;
	m_demo.reset();
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
	uc.process(1);

	constexpr uint8_t baseChannel = 0;

	switch (audioCallbackCount)
	{
	case 1:
		LOG("Sending Init Control Commands");
		uc.sendInitControlCommands();
		break;
	case 256:
		if(!m_demo)
		{
			LOG("Sending Preset");
#if 1
			uc.writeSingle(BankNumber::EditBuffer, 0, preset);	// cmdline
			uc.writeSingle(BankNumber::EditBuffer, 1, preset);
			v.getSingle(1, 6, preset);							// Anubis MS
			uc.writeSingle(BankNumber::EditBuffer, 2, preset);
			uc.writeSingle(BankNumber::EditBuffer, 3, preset);
			v.getSingle(10, 56, preset);						// Impact MS
			uc.writeSingle(BankNumber::EditBuffer, 4, preset);
			uc.writeSingle(BankNumber::EditBuffer, 5, preset);
#else
			uc.writeSingle(BankNumber::EditBuffer, virusLib::SINGLE, preset);
#endif
/*			uc.writeSingle(BankNumber::EditBuffer, 3, preset);
			uc.writeSingle(BankNumber::EditBuffer, 4, preset);
			uc.writeSingle(BankNumber::EditBuffer, 5, preset);
			uc.writeSingle(BankNumber::EditBuffer, 6, preset);
			uc.writeSingle(BankNumber::EditBuffer, 7, preset);
			uc.writeSingle(BankNumber::EditBuffer, 8, preset);
			uc.writeSingle(BankNumber::EditBuffer, 9, preset);
			uc.writeSingle(BankNumber::EditBuffer, 10, preset);
			uc.writeSingle(BankNumber::EditBuffer, 11, preset);
			uc.writeSingle(BankNumber::EditBuffer, 12, preset);
			uc.writeSingle(BankNumber::EditBuffer, 13, preset);
			uc.writeSingle(BankNumber::EditBuffer, 14, preset);
			uc.writeSingle(BankNumber::EditBuffer, 15, preset);
*/		}
		break;
	case 512:
		if(!m_demo)
		{
			LOG("Sending Note On");
//			uc.sendMIDI(SMidiEvent(0x90 + baseChannel, 36, 0x5f));	// Note On
//			uc.sendMIDI(SMidiEvent(0x90 + baseChannel, 48, 0x5f));	// Note On
			for(uint8_t i=0; i<6; ++i)
				uc.sendMIDI(SMidiEvent(0x90 + i, 60, 0x5f));		// Note On
//			uc.sendMIDI(SMidiEvent(0x90 + baseChannel, 60, 0x5f));	// Note On
//			uc.sendMIDI(SMidiEvent(0x90 + baseChannel, 63, 0x5f));	// Note On
//			uc.sendMIDI(SMidiEvent(0x90 + baseChannel, 67, 0x5f));	// Note On
//			uc.sendMIDI(SMidiEvent(0x90 + baseChannel, 72, 0x5f));	// Note On
//			uc.sendMIDI(SMidiEvent(0x90 + baseChannel, 75, 0x5f));	// Note On
//			uc.sendMIDI(SMidiEvent(0x90 + baseChannel, 79, 0x5f));	// Note On
//			uc.sendMIDI(SMidiEvent(0xb0, 1, 0));		// Modwheel 0
			uc.sendPendingMidiEvents(std::numeric_limits<uint32_t>::max());
		}
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
#if 0
	static uint8_t cycle = 0;

	static uint8_t channel = 0;
	static int totalNoteCount = 1;

	if(audioCallbackCount >= 1024 && (audioCallbackCount & 2047) == 0)
	{
		static uint8_t note = 127;
		if(note >= 96)
		{
			note = 24;

			switch(cycle)
			{
			case 0:				note += 0;				break;
			case 1:				note += 3;				break;
			case 2:				note += 7;				break;
			case 3:				note += 10;				break;
			case 4:				note += 5;				break;
			case 5:				note += 2;				break;
			}
			++cycle;
			if(cycle == 6)
				cycle = 0;
		}
		if(cycle < 7)
		{
			totalNoteCount++;
			LOG("Sending Note On for note " << static_cast<int>(note) << ", total notes " << totalNoteCount);
			uc.sendMIDI(SMidiEvent(0x90 + baseChannel + channel, note, 0x5f));	// Note On
			channel++;
			if(channel >= 6)
//			if(channel >= 16)
				channel = 0;
			uc.sendPendingMidiEvents(std::numeric_limits<uint32_t>::max());

//			if(totalNoteCount >= 40)
//				dsp.enableTrace(static_cast<dsp56k::DSP::TraceMode>(dsp56k::DSP::Ops | dsp56k::DSP::Regs | dsp56k::DSP::StackIndent));
		}
		note += 12;
	}
#endif
	if(m_demo && audioCallbackCount >= 256)
		m_demo->process(1);
}

void ConsoleApp::run(const std::string& _audioOutputFilename, EsaiListenerToCallback::TDataCallback _callback, uint32_t _maxSampleCount/* = 0*/)
{
	if (v.isTIFamily())
	{
		const auto cycles = 150 * 1000000 / (v.getSamplerate() * 3);

		periphX.getEsaiClock().setCyclesPerSample(cycles);

		periphX.getEsaiClock().setEsaiDivider(&periphY.getEsai(), 0);
		periphX.getEsaiClock().setEsaiDivider(&periphX.getEsai(), 2);
	}

	auto loader = bootDSP();

//	dsp.enableTrace((DSP::TraceMode)(DSP::Ops | DSP::Regs | DSP::StackIndent));

	const auto sr = v.getSamplerate();

	std::unique_ptr<EsaiListener> esaiListener;

	uint8_t esai0OutChannels = 0b000001;
	uint8_t esai0InChannels  = 0b0001;
	uint8_t esai1OutChannels = 0b000001;
	uint8_t esai1InChannels  = 0b0001;

	if(v.getModel() == ROMFile::Model::Snow)
	{
		esai1OutChannels = 0b011100;
	}
	else if(v.getModel() == ROMFile::Model::TI)
	{
		esai0OutChannels = 0b011100;
		esai0InChannels  = 0b0001;
		esai1OutChannels = 0b000011;
		esai1InChannels  = 0b0011;
	}

	if (!_audioOutputFilename.empty())
		esaiListener.reset(new EsaiListenerToFile(periphX.getEsai(), esai0OutChannels, esai0InChannels, [&](EsaiListener*, uint32_t _count) { audioCallback(_count); }, sr, _audioOutputFilename));
	else
		esaiListener.reset(new EsaiListenerToCallback(periphX.getEsai(), esai0OutChannels, esai0InChannels, [&](EsaiListener*, uint32_t _count) { audioCallback(_count); }, std::move(_callback)));

	EsaiListenerToFile esaiListener1(periphY.getEsai(), esai1OutChannels, esai1InChannels, [](EsaiListener*, uint32_t) {}, sr, "ESAI1_TI.wav");

	esaiListener->setMaxSamplecount(_maxSampleCount);

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

/*
	const std::string romFile = m_romName;
	auto& mem = memory;

	mem.saveAsText((romFile + "_X.txt").c_str(), dsp56k::MemArea_X, 0, mem.size());
	mem.saveAsText((romFile + "_Y.txt").c_str(), dsp56k::MemArea_Y, 0, mem.size());
	mem.save((romFile + "_P.bin").c_str(), dsp56k::MemArea_P);
//	mem.saveAssembly((romFile + "_P.asm").c_str(), 0, mem.size(), true, false, dsp.getPeriph(0), dsp.getPeriph(1));
*/
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
