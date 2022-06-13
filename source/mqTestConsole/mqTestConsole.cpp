#include <iostream>
#include <memory>

#include "../synthLib/os.h"
#include "../synthLib/wavWriter.h"
#include "../synthLib/midiTypes.h"

#include "../mqLib/mqmc.h"
#include "../mqLib/mqhardware.h"

#include "dsp56kEmu/dspthread.h"
#include "dsp56kEmu/interpreterunittests.h"
#include "dsp56kEmu/jitunittests.h"

#include <cpp-terminal/window.hpp>
#include <cpp-terminal/input.hpp>

#include <vector>

#include "mqGui.h"

using Term::Terminal;
using Term::Key;
using Term::Window;

using ButtonType = mqLib::Buttons::ButtonType;
using EncoderType = mqLib::Buttons::Encoders;

int main(int _argc, char* _argv[])
{
	try
	{
//		dsp56k::InterpreterUnitTests tests;
//		dsp56k::JitUnittests tests;
//		return 0;
	}
	catch(std::string& _err)
	{
		LOG("Unit Test failed: " << _err);
		return -1;
	}

	Terminal term(true, true, true, true);

	Window winMQ(120,50);

	const auto romFile = synthLib::findROM(512 * 1024);

	if(romFile.empty())
	{
		std::cout << "Failed to find ROM, make sure that a ROM file with extension .bin is placed next to this executable" << std::endl;
		return -1;
	}

	std::unique_ptr<mqLib::Hardware> hw;
	hw.reset(new mqLib::Hardware(romFile));

	auto& buttons = hw->getUC().getButtons();

	Gui gui(*hw);

	dsp56k::RingBuffer<int, 64, true> keyBuffer;

	std::thread inputReader([&hw, &keyBuffer]
	{
		while(hw.get())
		{
			const auto k = Term::read_key();
			if(k)
				keyBuffer.push_back(k);
		}
	});

	bool silence = true;

	const std::string filename = "mq_output_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) + ".wav";
	synthLib::AsyncWriter wavWriter(filename, 44100);

	constexpr uint32_t blockSize = 64;

	std::vector<dsp56k::TWord> m_stereoOutput;
	m_stereoOutput.resize(blockSize<<1);

	const auto startTime = std::chrono::system_clock::now();

	bool waitForBootKeys = true;

	dsp56k::RingBuffer<uint32_t, 1024, true> renderTrigger;

	std::thread renderer([&]
	{
		while(hw)
		{
			renderTrigger.pop_front();
			if(renderTrigger.empty())
			{
				gui.render();
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		}
	});

	std::thread oneSecondUpdater([&]
	{
		while(hw)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			renderTrigger.push_back(1);
		}
	});

	auto toggleButton = [&](mqLib::Buttons::ButtonType _type)
	{
		buttons.toggleButton(_type);
		renderTrigger.push_back(1);
	};

	auto encRotate = [&](mqLib::Buttons::Encoders _encoder, int _amount)
	{
		buttons.rotate(_encoder, _amount);
		renderTrigger.push_back(1);
	};

	hw->getUC().getLeds().setChangeCallback([&]()
	{
		renderTrigger.push_back(1);
	});

	hw->getUC().getLcd().setChangeCallback([&]()
	{
		renderTrigger.push_back(1);
	});

	while(true)
	{
		while(!keyBuffer.empty())
		{
			const auto ch = keyBuffer.pop_front();
			switch (ch)
			{
			case '1':					toggleButton(ButtonType::Inst1);			break;
			case '2':					toggleButton(ButtonType::Inst2);			break;
			case '3':					toggleButton(ButtonType::Inst3);			break;
			case '4':					toggleButton(ButtonType::Inst4);			break;
			case Key::ARROW_DOWN:		toggleButton(ButtonType::Down);				break;
			case Key::ARROW_LEFT:		toggleButton(ButtonType::Left);				break;
			case Key::ARROW_RIGHT:		toggleButton(ButtonType::Right);			break;
			case Key::ARROW_UP:			toggleButton(ButtonType::Up);				break;
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
				hw->sendMidi(synthLib::M_NOTEON);
				hw->sendMidi(synthLib::Note_C3);
				hw->sendMidi(0x7f);
				break;
			case '8':	
				// Midi Note Off
				hw->sendMidi(synthLib::M_NOTEOFF);
				hw->sendMidi(synthLib::Note_C3);
				hw->sendMidi(0x7f);
				break;
			case '9':
				// Modwheel Max
				hw->sendMidi(synthLib::M_CONTROLCHANGE);
				hw->sendMidi(synthLib::MC_MODULATION);
				hw->sendMidi(0x7f);
				break;
			case '0':	
				// Modwheel Min
				hw->sendMidi(synthLib::M_CONTROLCHANGE);
				hw->sendMidi(synthLib::MC_MODULATION);
				hw->sendMidi(0x0);
				break;
			case '!':
				hw->getDSP().dumpPMem("dsp_dump_P_" + std::to_string(hw->getDspCycles()));
				break;
			case '&':
				hw->getDSP().dumpXYMem("dsp_dump_mem_" + std::to_string(hw->getDspCycles()) + "_");
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

		if(waitForBootKeys)
		{
			const auto t = std::chrono::system_clock::now();
			const auto d = std::chrono::duration_cast<std::chrono::milliseconds>(t - startTime).count();

			if(d < 1000)
				continue;

			waitForBootKeys = false;
			LOG("Wait for boot keys over");
		}

		hw->process(blockSize);

		auto& outputs = hw->getAudioOutputs();

		for(size_t i=0; i<blockSize; ++i)
		{
			m_stereoOutput[i<<1] = outputs[0][i];
			m_stereoOutput[(i<<1) + 1] = outputs[1][i];

			if(silence && (outputs[0][i] || outputs[1][i]))
				silence = false;
		}

		if(!silence)
		{
			wavWriter.append([&](auto& _dst)
			{
				_dst.reserve(_dst.size() + m_stereoOutput.size());
				for (auto& d : m_stereoOutput)
					_dst.push_back(d);
			}
			);
		}
	}
	
	return 0;
}
