#include "consoleApp.h"

#include <iostream>

#include "esaiListenerToFile.h"
#include "../synthLib/audiobuffer.h"

#include "../virusLib/device.h"
#include "../virusLib/midiOutParser.h"
#include "../virusLib/demoplaybackTI.h"

namespace virusLib
{
	class Device;
}

using namespace virusLib;
using namespace synthLib;

class EsaiListener;

ConsoleApp::ConsoleApp(const std::string& _romFile)
: m_romName(_romFile)
, m_rom(_romFile, ROMFile::TIModel::TI)
, m_preset({})
{
	if (!m_rom.isValid())
	{
		std::cout << "ROM file " << _romFile << " is not valid and couldn't be loaded. Place a valid ROM file with .bin extension next to this program." << std::endl;
		return;
	}

	virusLib::DspSingle* dsp1 = nullptr;
	virusLib::Device::createDspInstances(dsp1, m_dsp2, m_rom);
	m_dsp1.reset(dsp1);

	uc.reset(new Microcontroller(m_dsp1->getHDI08(), m_rom));
	if(m_dsp2)
		uc->addHDI08(m_dsp2->getHDI08());
}

ConsoleApp::~ConsoleApp()
{
	m_demo.reset();
	uc.reset();
	m_dsp1.reset();
	m_dsp2 = nullptr;
}

bool ConsoleApp::isValid() const
{
	return m_rom.isValid();
}

void ConsoleApp::waitReturn()
{
	std::cin.ignore();
}

std::thread ConsoleApp::bootDSP() const
{
	auto loader = virusLib::Device::bootDSP(*m_dsp1, m_rom);

	if(m_dsp2)
	{
		auto loader2 = virusLib::Device::bootDSP(*m_dsp2, m_rom);
		loader2.join();
	}

	return loader;
}

dsp56k::IPeripherals& ConsoleApp::getYPeripherals() const
{
	if (m_rom.isTIFamily())
		return m_dsp1->getPeriphY();

	return m_dsp1->getPeriphNop();
}

void ConsoleApp::loadSingle(int b, int p)
{
	if(m_rom.getSingle(b, p, m_preset))
	{
		std::cout << "Loaded Single " << ROMFile::getSingleName(m_preset) << std::endl;
	}
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
		const int bank = preset / m_rom.getPresetsPerBank();
		preset -= bank * m_rom.getPresetsPerBank();
		loadSingle(bank, preset);
		return true;
	}

	for (uint32_t b = 0; b < 26; ++b)
	{
		for (uint32_t p = 0; p < m_rom.getPresetsPerBank(); ++p)
		{
			Microcontroller::TPreset data;
			m_rom.getSingle(b, p, data);

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
	m_demo.reset(m_rom.isTIFamily() ? new DemoPlaybackTI(*uc) : new DemoPlayback(*uc));

	if(m_demo->loadFile(_filename))
	{
		std::cout << "Loaded demo song from file " << m_demo->loadFile(_filename) << std::endl;
		return true;
	}

	m_demo.reset();
	return false;
}

bool ConsoleApp::loadInternalDemo()
{
	if(m_rom.getDemoData().empty())
		return false;

	m_demo.reset(m_rom.isTIFamily() ? new DemoPlaybackTI(*uc) : new DemoPlayback(*uc));

	if(m_demo->loadBinData(m_rom.getDemoData()))
	{
		std::cout << "Loaded internal demo from ROM " << m_romName << std::endl;
		return true;
	}
	m_demo.reset();
	return false;
}

std::string ConsoleApp::getSingleName() const
{
	return ROMFile::getSingleName(m_preset);
}

std::string ConsoleApp::getSingleNameAsFilename() const
{
	auto audioFilename = m_demo ? "factorydemo" : getSingleName();

	for (size_t i = 0; i < audioFilename.size(); ++i)
	{
		if (audioFilename[i] == ' ')
			audioFilename[i] = '_';
	}
	return "virusEmu_" + audioFilename + ".wav";
}

void ConsoleApp::audioCallback(uint32_t audioCallbackCount)
{
	uc->process(1);

	constexpr uint8_t baseChannel = 0;

	switch (audioCallbackCount)
	{
	case 1:
		LOG("Sending Init Control Commands");
		uc->sendInitControlCommands();
		break;
	case 256:
		if(!m_demo)
		{
			LOG("Sending Preset");
#if 0
			uc->writeSingle(BankNumber::EditBuffer, 0, m_preset);	// cmdline
			v.getSingle(1, 6, m_preset);							// Anubis MS
			uc->writeSingle(BankNumber::EditBuffer, 1, m_preset);
			v.getSingle(1, 10, m_preset);							// Impact MS
			uc->writeSingle(BankNumber::EditBuffer, 2, m_preset);
			v.getSingle(1, 3, m_preset);							// Impact MS
			uc->writeSingle(BankNumber::EditBuffer, 3, m_preset);
#elif 0
			uc->writeSingle(BankNumber::EditBuffer, 0, m_preset);	// cmdline
			uc->writeSingle(BankNumber::EditBuffer, 1, m_preset);
			v.getSingle(1, 6, m_preset);							// Anubis MS
			uc->writeSingle(BankNumber::EditBuffer, 2, m_preset);
			uc->writeSingle(BankNumber::EditBuffer, 3, m_preset);
			v.getSingle(10, 56, m_preset);							// Impact MS
			uc->writeSingle(BankNumber::EditBuffer, 4, m_preset);
			uc->writeSingle(BankNumber::EditBuffer, 5, m_preset);
#else
			uc->writeSingle(BankNumber::EditBuffer, virusLib::SINGLE, m_preset);
#endif
/*			uc->writeSingle(BankNumber::EditBuffer, 3, m_preset);
			uc->writeSingle(BankNumber::EditBuffer, 4, m_preset);
			uc->writeSingle(BankNumber::EditBuffer, 5, m_preset);
			uc->writeSingle(BankNumber::EditBuffer, 6, m_preset);
			uc->writeSingle(BankNumber::EditBuffer, 7, m_preset);
			uc->writeSingle(BankNumber::EditBuffer, 8, m_preset);
			uc->writeSingle(BankNumber::EditBuffer, 9, m_preset);
			uc->writeSingle(BankNumber::EditBuffer, 10, m_preset);
			uc->writeSingle(BankNumber::EditBuffer, 11, m_preset);
			uc->writeSingle(BankNumber::EditBuffer, 12, m_preset);
			uc->writeSingle(BankNumber::EditBuffer, 13, m_preset);
			uc->writeSingle(BankNumber::EditBuffer, 14, m_preset);
			uc->writeSingle(BankNumber::EditBuffer, 15, m_preset);
*/		}
		break;
	case 512:
		if(!m_demo)
		{
			LOG("Sending Note On");
//			uc->sendMIDI(SMidiEvent(0x90 + baseChannel, 36, 0x5f));	// Note On
//			uc->sendMIDI(SMidiEvent(0x90 + baseChannel, 48, 0x5f));	// Note On
			for(uint8_t i=0; i<1; ++i)
				uc->sendMIDI(SMidiEvent(0x90 + i, 60, 0x5f));		// Note On
//			uc->sendMIDI(SMidiEvent(0x90 + baseChannel, 60, 0x5f));	// Note On
//			uc->sendMIDI(SMidiEvent(0x90 + baseChannel, 63, 0x5f));	// Note On
//			uc->sendMIDI(SMidiEvent(0x90 + baseChannel, 67, 0x5f));	// Note On
//			uc->sendMIDI(SMidiEvent(0x90 + baseChannel, 72, 0x5f));	// Note On
//			uc->sendMIDI(SMidiEvent(0x90 + baseChannel, 75, 0x5f));	// Note On
//			uc->sendMIDI(SMidiEvent(0x90 + baseChannel, 79, 0x5f));	// Note On
//			uc->sendMIDI(SMidiEvent(0xb0, 1, 0));		// Modwheel 0
			uc->sendPendingMidiEvents(std::numeric_limits<uint32_t>::max());
		}
		break;
/*	case 8000:
		LOG("Sending 2nd Note On");
		uc->sendMIDI(SMidiEvent(0x90, 67, 0x7f));	// Note On
		uc->sendPendingMidiEvents(std::numeric_limits<uint32_t>::max());
		break;
	case 16000:
		LOG("Sending 3rd Note On");
		uc->sendMIDI(SMidiEvent(0x90, 63, 0x7f));	// Note On
		uc->sendPendingMidiEvents(std::numeric_limits<uint32_t>::max());
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
			uc->sendMIDI(SMidiEvent(0x90 + baseChannel + channel, note, 0x5f));	// Note On
			channel++;
			if(channel >= 6)
//			if(channel >= 16)
				channel = 0;
			uc->sendPendingMidiEvents(std::numeric_limits<uint32_t>::max());

//			if(totalNoteCount >= 40)
//				dsp.enableTrace(static_cast<dsp56k::DSP::TraceMode>(dsp56k::DSP::Ops | dsp56k::DSP::Regs | dsp56k::DSP::StackIndent));
		}
		note += 12;
	}
#endif
	if(m_demo && audioCallbackCount >= 256)
		m_demo->process(1);
}

void ConsoleApp::run(const std::string& _audioOutputFilename, uint32_t _maxSampleCount/* = 0*/)
{
	assert(!_audioOutputFilename.empty());
//	dsp.enableTrace((DSP::TraceMode)(DSP::Ops | DSP::Regs | DSP::StackIndent));

	const auto sr = m_rom.getSamplerate();

	uint32_t callbackCount = 0;

	m_dsp1->getPeriphX().getEsai().setCallback([&](dsp56k::Audio*)
	{
		callbackCount++;
		if((callbackCount & 0x07) == 0)
			audioCallback(callbackCount>>3);
	}, 0);

	bootDSP().join();
	/*
	const std::string romFile = m_romName;
	auto& mem = m_dsp1->getMemory();

	mem.saveAsText((romFile + "_X.txt").c_str(), dsp56k::MemArea_X, 0, mem.size());
	mem.saveAsText((romFile + "_Y.txt").c_str(), dsp56k::MemArea_Y, 0, mem.size());
	mem.save((romFile + "_P.bin").c_str(), dsp56k::MemArea_P);
	mem.saveAssembly((romFile + "_P.asm").c_str(), 0, mem.size(), true, false, m_dsp1->getDSP().getPeriph(0), m_dsp1->getDSP().getPeriph(1));
	*/
	MidiOutParser midiOut;

	constexpr uint32_t blockSize = 64;

	uint32_t processedSampleCount = 0;

	synthLib::TAudioInputsInt inputs{};
	synthLib::TAudioOutputsInt outputs{};

	std::vector<std::vector<dsp56k::TWord>> output;
	std::vector<std::vector<dsp56k::TWord>> input;

	output.resize(2);
	input.resize(2);

	for(size_t i=0; i<output.size(); ++i)
	{
		output[i].resize(blockSize);
		input[i].resize(blockSize);

		inputs[i] = &input[i][0];
		outputs[i] = &output[i][0];
	}

	std::mutex writeMutex;

	std::vector<dsp56k::TWord> mixBuffer;
	mixBuffer.reserve(output.size() * 2);

	bool runThread = true;

	std::thread threadWrite([&]()
	{
		WavWriter writer;

		std::vector<dsp56k::TWord> wordBuffer;
		wordBuffer.reserve(output.size() * 2);
		std::vector<uint8_t> byteBuffer;
		byteBuffer.reserve(wordBuffer.capacity() * 3);

		while(runThread)
		{
			{
				std::lock_guard lock(writeMutex);
				std::swap(wordBuffer, mixBuffer);
			}

			if(!wordBuffer.empty())
			{
				for (dsp56k::TWord w : wordBuffer)
					EsaiListenerToFile::writeWord(byteBuffer, w);
				wordBuffer.clear();

				if(writer.write(_audioOutputFilename, 24, false, 2, static_cast<int>(m_rom.getSamplerate()), &byteBuffer[0], byteBuffer.size()))
					byteBuffer.clear();

				std::this_thread::sleep_for(std::chrono::milliseconds(500));
			}
		}
	});

	while(true)
	{
		auto sampleCount = static_cast<uint32_t>(input.size());

		if(_maxSampleCount && processedSampleCount >= _maxSampleCount)
			break;

		if(_maxSampleCount && _maxSampleCount - processedSampleCount < sampleCount)
		{
			sampleCount = _maxSampleCount - processedSampleCount;
		}

		m_dsp1->processAudio(inputs, outputs, sampleCount, 0);

		processedSampleCount += sampleCount;

		{
			std::lock_guard lock(writeMutex);

			mixBuffer.reserve(mixBuffer.size() + sampleCount * 2);

			for(size_t iSrc=0; iSrc<sampleCount; ++iSrc)
			{
				mixBuffer.push_back(outputs[0][iSrc]);
				mixBuffer.push_back(outputs[1][iSrc]);
			}
		}

		while(m_dsp1->getPeriphX().getHDI08().hasTX())
		{
			const auto word = m_dsp1->getPeriphX().getHDI08().readTX();
			midiOut.append(word);
		}

		while(m_dsp2->getPeriphX().getHDI08().hasTX())
			m_dsp2->getPeriphX().getHDI08().readTX();
	}

	runThread = false;
	threadWrite.join();
}
