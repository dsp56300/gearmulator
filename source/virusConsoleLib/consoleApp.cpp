#include "consoleApp.h"

#include <iostream>

#include "audioProcessor.h"
#include "esaiListenerToFile.h"

#include "virusLib/device.h"
#include "virusLib/romloader.h"
#include "virusLib/demoplaybackTI.h"

#include "dsp56kEmu/dsp.h"

namespace virusLib
{
	class Device;
}

using namespace virusLib;
using namespace synthLib;

class EsaiListener;

namespace
{
	ROMFile findRom(const std::string& _name, const DeviceModel _tiModel)
	{
		auto result = ROMLoader::findROM(_name, _tiModel);
		if(result.isValid())
			return result;
		return ROMLoader::findROM(_name, DeviceModel::ABC);
	}
}

ConsoleApp::ConsoleApp(const std::string& _romFile, const DeviceModel _tiModel)
: m_romName(_romFile)
, m_rom(findRom(_romFile, _tiModel))
, m_preset({})
{
	if (!m_rom.isValid())
	{
		std::cout << "ROM file " << _romFile << " is not valid and couldn't be loaded. Place a valid ROM file with .bin extension next to this program." << std::endl;
		return;
	}

	std::cout << "Using ROM " << m_rom.getFilename() << '\n';

	virusLib::DspSingle* dsp1 = nullptr;
	virusLib::Device::createDspInstances(dsp1, m_dsp2, m_rom, static_cast<float>(m_rom.getSamplerate()));
	m_dsp1.reset(dsp1);

	m_uc.reset(new Microcontroller(*m_dsp1, m_rom, false));
	if(m_dsp2)
		m_uc->addDSP(*m_dsp2, false);
}

ConsoleApp::~ConsoleApp()
{
	destroy();
}

bool ConsoleApp::isValid() const
{
	return m_rom.isValid();
}

void ConsoleApp::waitReturn()
{
	std::cin.ignore();
}

void ConsoleApp::bootDSP(const bool _createDebugger) const
{
	virusLib::Device::bootDSPs(m_dsp1.get(), m_dsp2, m_rom, _createDebugger);
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
	m_demo.reset(m_rom.isTIFamily() ? new DemoPlaybackTI(*m_uc) : new DemoPlayback(*m_uc));

	if(m_demo->loadFile(_filename))
	{
		std::cout << "Loaded demo song from file " << _filename << std::endl;
		return true;
	}

	m_demo.reset();
	return false;
}

bool ConsoleApp::loadInternalDemo()
{
	if(m_rom.getDemoData().empty())
		return false;

	m_demo.reset(m_rom.isTIFamily() ? new DemoPlaybackTI(*m_uc) : new DemoPlayback(*m_uc));

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

void ConsoleApp::audioCallback(const uint32_t _audioCallbackCount)
{
	m_uc->process();

	switch (_audioCallbackCount)
	{
	case 1:
		m_dsp1->drainESSI1();
		LOG("Sending Init Control Commands");
		m_uc->sendInitControlCommands(127);	// set Master Volume to max
		break;
	case 256:
		m_dsp1->drainESSI1();
		m_dsp1->disableESSI1();
		if(!m_demo)
		{
			LOG("Sending Preset");
			m_uc->writeSingle(BankNumber::EditBuffer, virusLib::SINGLE, m_preset);
		}
		break;
	case 512:
		if(!m_demo)
		{
			LOG("Sending Note On");
			m_uc->sendMIDI(SMidiEvent(MidiEventSource::Host, 0x90, 60, 0x5f));		// Note On
			m_uc->sendPendingMidiEvents(std::numeric_limits<uint32_t>::max());
		}
		break;
	}

	if(m_demo && _audioCallbackCount >= 256)
		m_demo->process(1);
}

void ConsoleApp::destroy()
{
	m_demo.reset();
	m_uc.reset();
	m_dsp1.reset();
	m_dsp2 = nullptr;
}

void ConsoleApp::run(const std::string& _audioOutputFilename, uint32_t _maxSampleCount/* = 0*/, uint32_t _blockSize/* = 64*/, bool _createDebugger/* = false*/, bool _dumpAssembler/* = false*/)
{
	assert(!_audioOutputFilename.empty());
//	dsp.enableTrace((DSP::TraceMode)(DSP::Ops | DSP::Regs | DSP::StackIndent));

	const uint32_t blockSize = _blockSize;
	const uint32_t notifyThreshold = blockSize > 4 ? blockSize - 4 : 0;

	uint32_t callbackCount = 0;
	dsp56k::Semaphore sem(1);

	auto& esai = m_dsp1->getAudio();
	int32_t notifyTimeout = 0;

	std::vector<synthLib::SMidiEvent> midiEvents;

	esai.setCallback([&](dsp56k::Audio*)
	{
		// Reduce thread contention by waiting until we have nearly enough audio output data available.
		// The DSP thread needs to lock & unlock a mutex to inform the waiting thread (us) that data is
		// available if the output ring buffer was completely drained. We can omit this by ensuring that
		// the output buffer never becomes completely empty.
		const auto availableSize = esai.getAudioOutputs().size();
		const auto sizeReached = availableSize >= notifyThreshold;

		--notifyTimeout;

//		LOG("Size " << esai.getAudioOutputs().size() << ", size reached " << (sizeReached ? "true" : "false") << ", notify " << (notify ? "true" : "false"));
		if(notifyTimeout <= 0 && sizeReached)
		{
			notifyTimeout = static_cast<int>(notifyThreshold);
			sem.notify();
		}

		callbackCount++;
		if((callbackCount & 0x3) == 0)
		{
			m_uc->readMidiOut(midiEvents);
			audioCallback(callbackCount>>2);
		}
	}, 0);

	bootDSP(_createDebugger);

	if(_dumpAssembler)
	{
		const std::string romFile = m_rom.getFilename();
		auto& mem = m_dsp1->getMemory();

		mem.saveAsText((romFile + "_X.txt").c_str(), dsp56k::MemArea_X, 0, mem.sizeXY());
		mem.saveAsText((romFile + "_Y.txt").c_str(), dsp56k::MemArea_Y, 0, mem.sizeXY());
		mem.save((romFile + "_P.bin").c_str(), dsp56k::MemArea_P);
		mem.saveAssembly((romFile + "_P.asm").c_str(), 0, mem.sizeP(), true, false, m_dsp1->getDSP().getPeriph(0), m_dsp1->getDSP().getPeriph(1));
	}

	AudioProcessor proc(m_rom.getSamplerate(), _audioOutputFilename, m_demo != nullptr, _maxSampleCount, m_dsp1.get(), m_dsp2);

	while(!proc.finished())
	{
		sem.wait();
		proc.processBlock(blockSize);
		midiEvents.clear();
	}

	m_dsp1.reset();
	destroy();
}
