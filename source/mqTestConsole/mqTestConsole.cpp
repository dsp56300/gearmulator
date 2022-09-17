#include <fstream>
#include <iostream>
#include <memory>

#include "../synthLib/os.h"
#include "../synthLib/wavWriter.h"
#include "../synthLib/midiTypes.h"
#include "../synthLib/configFile.h"

#include "../mqLib/microq.h"
#include "../mqLib/mqhardware.h"

#include "dsp56kEmu/dspthread.h"

#include "dsp56kEmu/jitunittests.h"

#include <cpp-terminal/input.hpp>

#include <vector>

#include "audioOutputPA.h"
#include "audioOutputWAV.h"
#include "midiInput.h"
#include "midiOutput.h"
#include "mqGui.h"
#include "mqSettingsGui.h"

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
	mqLib::MicroQ mq;
	auto* hw = mq.getHardware();

	// create terminal-GUI
	Terminal term(true, true, false, true);
	Gui gui(mq);

	SettingsGui settings;

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
		dsp56k::DSPThread::setCurrentThreadName("guiRender");
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
		dsp56k::DSPThread::setCurrentThreadName("1secUpdater");
		while(!exit)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			renderTrigger.push_back(1);
		}
	});

	auto toggleButton = [&](const mqLib::Buttons::ButtonType _type)
	{
		mq.setButton(_type, mq.getButton(_type) ? false : true);
		renderTrigger.push_back(1);
	};

	auto encRotate = [&](mqLib::Buttons::Encoders _encoder, const int _amount)
	{
		mq.rotateEncoder(_encoder, _amount);
		renderTrigger.push_back(1);
	};

	// we need a bit of time to press boot buttons (service mode, factory tests, etc)
	auto processKey = [&](int ch)
	{
		{
			switch (ch)
			{
			case Key::ENTER:
				if(showSettings)
				{
					settings.onEnter();
					renderTrigger.push_back(1);
				}
				break;
			case Key::ESC:
				showSettings = !showSettings;
				renderTrigger.push_back(1);
				break;
			case '1':					toggleButton(ButtonType::Inst1);			break;
			case '2':					toggleButton(ButtonType::Inst2);			break;
			case '3':					toggleButton(ButtonType::Inst3);			break;
			case '4':					toggleButton(ButtonType::Inst4);			break;
			case Key::ARROW_DOWN:
				if(showSettings)
				{
					settings.onDown();
					renderTrigger.push_back(1);
				}
				else
					toggleButton(ButtonType::Down);
				break;
			case Key::ARROW_LEFT:
				if(showSettings)
				{
					settings.onLeft();
					renderTrigger.push_back(1);
				}
				else
					toggleButton(ButtonType::Left);
				break;
			case Key::ARROW_RIGHT:
				if(showSettings)
				{
					settings.onRight();
					renderTrigger.push_back(1);
				}
				else
					toggleButton(ButtonType::Right);
				break;
			case Key::ARROW_UP:
				if(showSettings)
				{
					settings.onUp();
					renderTrigger.push_back(1);
				}
				else
					toggleButton(ButtonType::Up);
				break;
			case 'g':					toggleButton(ButtonType::Global);			break;
			case 'm':					toggleButton(ButtonType::Multi);			break;
			case 'e':					toggleButton(ButtonType::Edit);				break;
			case 's':					toggleButton(ButtonType::Sound);			break;
			case 'S':					toggleButton(ButtonType::Shift);			break;
			case 'q':					toggleButton(ButtonType::Power);			break;
			case 'M':					toggleButton(ButtonType::Multimode);		break;
			case 'P':					toggleButton(ButtonType::Peek);				break;
			case 'p':					toggleButton(ButtonType::Play);				break;

			case Key::F1:				encRotate(EncoderType::LcdLeft, -1);		break;
			case Key::F2:				encRotate(EncoderType::LcdLeft, 1);			break;
			case Key::F3:				encRotate(EncoderType::LcdRight, -1);		break;
			case Key::F4:				encRotate(EncoderType::LcdRight, 1);		break;
			case '5':					encRotate(EncoderType::Master, -1);			break;
			case '6':					encRotate(EncoderType::Master, 1);			break;
			case Key::F5:				encRotate(EncoderType::Matrix1, -1);		break;
			case Key::F6:				encRotate(EncoderType::Matrix1, 1);			break;
			case Key::F7:				encRotate(EncoderType::Matrix2, -1);		break;
			case Key::F8:				encRotate(EncoderType::Matrix2, 1);			break;
			case Key::F9:				encRotate(EncoderType::Matrix3, -1);		break;
			case Key::F10:				encRotate(EncoderType::Matrix3, 1);			break;
			case Key::F11:				encRotate(EncoderType::Matrix4, -1);		break;
			case Key::F12:				encRotate(EncoderType::Matrix4, 1);			break;
			case '7':
				// Midi Note On
				mq.sendMidi(synthLib::M_NOTEON);
				mq.sendMidi(synthLib::Note_C3);
				mq.sendMidi(0x7f);
				break;
			case '8':	
				// Midi Note Off
				mq.sendMidi(synthLib::M_NOTEOFF);
				mq.sendMidi(synthLib::Note_C3);
				mq.sendMidi(0x7f);
				break;
			case '9':
				// Modwheel Max
				mq.sendMidi(synthLib::M_CONTROLCHANGE);
				mq.sendMidi(synthLib::MC_MODULATION);
				mq.sendMidi(0x7f);
				break;
			case '0':	
				// Modwheel Min
				mq.sendMidi(synthLib::M_CONTROLCHANGE);
				mq.sendMidi(synthLib::MC_MODULATION);
				mq.sendMidi(0x0);
				break;
			case '!':
				hw->getDSP().dumpPMem("dsp_dump_P_" + std::to_string(hw->getUcCycles()));
				break;
			case '&':
				hw->getDSP().dumpXYMem("dsp_dump_mem_" + std::to_string(hw->getUcCycles()) + "_");
				break;
			case '"':
				hw->getUC().dumpMemory("mc_dump_mem");
				break;
			case '$':
				hw->getUC().dumpROM("rom_runtime");
				break;
			default:
				break;
			}
		}
	};

	// threaded key reader
	std::thread inputReader([&exit, &processKey]
	{
		dsp56k::DSPThread::setCurrentThreadName("inputReader");
		while(!exit)
		{
			const auto k = Term::read_key();
			if(k)
				processKey(k);
		}
	});

	std::unique_ptr<AudioOutputPA> audio;
	std::unique_ptr<MidiInput> midiIn;
	std::unique_ptr<MidiOutput> midiOut;

	std::vector<synthLib::SMidiEvent> midiInBuffer;
	std::vector<uint8_t> midiOutBuffer;

	std::mutex mutexDevices;

	std::function process = [&](uint32_t _blockSize, const mqLib::TAudioOutputs*& _dst)
	{
		std::lock_guard lockDevices(mutexDevices);

		if(midiIn)
			midiIn->process(midiInBuffer);

		for (const auto& e : midiInBuffer)
			mq.sendMidiEvent(e);
		midiInBuffer.clear();

		mq.process(_blockSize);
		_dst = &mq.getAudioOutputs();

		if(mq.getDirtyFlags() != mqLib::MicroQ::DirtyFlags::None)
			renderTrigger.push_back(1);

		mq.receiveMidi(midiOutBuffer);
		if(midiOut)
			midiOut->write(midiOutBuffer);
		midiOutBuffer.clear();
	};
	
	auto createDevices = [&]()
	{
		std::lock_guard lockDevices(mutexDevices);

		if(!audio || (!devNameAudioOut.empty() && audio->getDeviceName() != devNameAudioOut))
		{
			audio.reset();
			audio.reset(new AudioOutputPA(process, devNameAudioOut));
		}
		if(!midiIn || (!devNameMidiIn.empty() && midiIn->getDeviceName() != devNameMidiIn))
		{
			midiIn.reset();
			midiIn.reset(new MidiInput(devNameMidiIn));
		}
		if(!midiOut || (!devNameMidiOut.empty() && midiOut->getDeviceName() != devNameMidiOut))
		{
			midiOut.reset();
			midiOut.reset(new MidiOutput(devNameMidiOut));
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

	dsp56k::DSPThread::setCurrentThreadName("main");

	while(true)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(250));

		if(settingsChanged)
		{
			createDevices();
			settingsChanged = false;
		}
	}

	return 0;
}
