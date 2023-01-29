#include <fstream>
#include <iostream>
#include <memory>

#include "../synthLib/os.h"
#include "../synthLib/wavWriter.h"
#include "../synthLib/midiTypes.h"
#include "../synthLib/configFile.h"

#include "../mqLib/microq.h"
#include "../mqLib/mqhardware.h"

#include "dsp56kEmu/threadtools.h"

#include "dsp56kEmu/jitunittests.h"

#include <cpp-terminal/input.hpp>

#include <vector>

#include "../mqConsoleLib/audioOutputPA.h"
#include "../mqConsoleLib/midiInput.h"
#include "../mqConsoleLib/midiOutput.h"
#include "../mqConsoleLib/mqGui.h"
#include "../mqConsoleLib/mqKeyInput.h"

#include "../mqConsoleLib/mqSettingsGui.h"

using Term::Terminal;
using Term::Key;

using ButtonType = mqLib::Buttons::ButtonType;
using EncoderType = mqLib::Buttons::Encoders;

int main(int _argc, char* _argv[])
{
	try
	{
//		dsp56k::InterpreterUnitTests tests;		// only valid if Interpreter is active
//		dsp56k::JitUnittests tests;				// only valid if JIT runtime is active
//		return 0;
	}
	catch(std::string& _err)
	{
		LOG("Unit Test failed: " << _err);
		return -1;
	}

	auto exit = false;

	// create hardware
	mqLib::MicroQ mq(mqLib::BootMode::Default);

	// create terminal-GUI
	Terminal term(true, true, false, true);
	mqConsoleLib::Gui gui(mq);

	mqConsoleLib::SettingsGui settings;

	int devIdMidiOut = -1;
	int devIdMidiIn = -1;
	int devIdAudioOut = -1;

	std::string devNameMidiIn;
	std::string devNameMidiOut;
	std::string devNameAudioOut;

	try
	{
		synthLib::ConfigFile cfg((synthLib::getModulePath() + "config.cfg").c_str());
		for (const auto& v : cfg.getValues())
		{
			if(v.first == "MidiIn")
				devNameMidiIn = v.second;
			else if(v.first == "MidiOut")
				devNameMidiOut = v.second;
			else if(v.first == "AudioOut")
				devNameAudioOut = v.second;
		}
	}
	catch(const std::runtime_error&)
	{
		// no config file available
	}

	bool settingsChanged = false;
	bool showSettings = false;

	// do not continously render our terminal GUI but only if something has changed
	dsp56k::RingBuffer<uint32_t, 1024, true> renderTrigger;

	std::thread renderer([&]
	{
		dsp56k::ThreadTools::setCurrentThreadName("guiRender");
		bool prevShowSettings = true;

		while(!exit)
		{
			renderTrigger.pop_front();
			if(renderTrigger.empty())
			{
				if(prevShowSettings != showSettings)
				{
					prevShowSettings = showSettings;
					if(showSettings)
						settings.onOpen();
					else
						gui.onOpen();
				}
				if(showSettings)
				{
					settings.render(devIdMidiIn, devIdMidiOut, devIdAudioOut);

					bool changed = true;
					if(!settings.getMidiInput().empty())
						devNameMidiIn = settings.getMidiInput();
					else if(!settings.getMidiOutput().empty())
						devNameMidiOut = settings.getMidiOutput();
					else if(!settings.getAudioOutput().empty())
						devNameAudioOut = settings.getAudioOutput();
					else
						changed = false;

					if(changed)
						settingsChanged = true;
				}
				else
					gui.render();
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		}
	});

	std::thread oneSecondUpdater([&]
	{
		dsp56k::ThreadTools::setCurrentThreadName("1secUpdater");
		while(!exit)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			renderTrigger.push_back(1);
		}
	});

	mqConsoleLib::KeyInput mqKeyInput(mq);

	// threaded key reader
	std::thread inputReader([&]
	{
		dsp56k::ThreadTools::setCurrentThreadName("inputReader");
		while(!exit)
		{
			const auto k = Term::read_key();
			if(k)
			{
				if(k == Key::ESC)
					showSettings = !showSettings;
				if(showSettings)
					settings.processKey(k);
				else
					mqKeyInput.processKey(k);
				renderTrigger.push_back(1);
			}
		}
	});

	std::unique_ptr<mqConsoleLib::AudioOutputPA> audio;
	std::unique_ptr<mqConsoleLib::MidiInput> midiIn;
	std::unique_ptr<mqConsoleLib::MidiOutput> midiOut;

	std::vector<synthLib::SMidiEvent> midiInBuffer;
	std::vector<uint8_t> midiOutBuffer;

	std::mutex mutexDevices;

	std::function process = [&](uint32_t _blockSize, const mqLib::TAudioOutputs*& _dst)
	{
		mq.process(_blockSize);
		_dst = &mq.getAudioOutputs();

		if(mq.getDirtyFlags() != mqLib::MicroQ::DirtyFlags::None)
			renderTrigger.push_back(1);
	};
	
	auto createDevices = [&]()
	{
		std::lock_guard lockDevices(mutexDevices);

		if(!audio || (!devNameAudioOut.empty() && audio->getDeviceName() != devNameAudioOut))
		{
			audio.reset();
			audio.reset(new mqConsoleLib::AudioOutputPA(process, devNameAudioOut));
		}
		if(!midiIn || (!devNameMidiIn.empty() && midiIn->getDeviceName() != devNameMidiIn))
		{
			midiIn.reset();
			midiIn.reset(new mqConsoleLib::MidiInput(devNameMidiIn));
		}
		if(!midiOut || (!devNameMidiOut.empty() && midiOut->getDeviceName() != devNameMidiOut))
		{
			midiOut.reset();
			midiOut.reset(new mqConsoleLib::MidiOutput(devNameMidiOut));
		}

		devNameMidiIn.clear();
		devNameMidiOut.clear();
		devNameAudioOut.clear();

		std::ofstream fs(synthLib::getModulePath() + "config.cfg");

		if(fs.is_open())
		{
			fs << "MidiIn=" << midiIn->getDeviceName() << std::endl;
			fs << "MidiOut=" << midiOut->getDeviceName() << std::endl;
			fs << "AudioOut=" << audio->getDeviceName() << std::endl;
			fs.close();
		}

		devIdMidiIn = midiIn->getDeviceId();
		devIdMidiOut = midiOut->getDeviceId();
		devIdAudioOut = audio->getDeviceId();
		settingsChanged = false;
	};

	createDevices();

	dsp56k::ThreadTools::setCurrentThreadName("main");

	while(true)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		if(settingsChanged)
		{
			createDevices();
			settingsChanged = false;
		}

		if(midiIn)
			midiIn->process(midiInBuffer);

		for (const auto& e : midiInBuffer)
			mq.sendMidiEvent(e);
		midiInBuffer.clear();

		mq.receiveMidi(midiOutBuffer);
		if(midiOut)
			midiOut->write(midiOutBuffer);
		midiOutBuffer.clear();
	}
}
