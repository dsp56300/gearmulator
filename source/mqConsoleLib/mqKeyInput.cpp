#include "mqKeyInput.h"

#include <cpp-terminal/input.hpp>

#include "../mqLib/buttons.h"
#include "../mqLib/microq.h"
#include "../mqLib/mqhardware.h"

#include "../synthLib/midiTypes.h"

using Key = Term::Key;
using EncoderType = mqLib::Buttons::Encoders;
using ButtonType = mqLib::Buttons::ButtonType;

namespace mqConsoleLib
{
	void KeyInput::processKey(int ch)
	{
		auto& mq = m_mq;
		auto* hw = mq.getHardware();

		auto toggleButton = [&](const mqLib::Buttons::ButtonType _type)
		{
			mq.setButton(_type, mq.getButton(_type) ? false : true);
		};

		auto encRotate = [&](mqLib::Buttons::Encoders _encoder, const int _amount)
		{
			mq.rotateEncoder(_encoder, _amount);
		};

		{
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
				mq.sendMidiEvent({synthLib::M_NOTEON, synthLib::Note_C3, 0x7f});
				break;
			case '8':	
				// Midi Note Off
				mq.sendMidiEvent({synthLib::M_NOTEOFF, synthLib::Note_C3, 0x7f});
				break;
			case '9':
				// Modwheel Max
				mq.sendMidiEvent({synthLib::M_CONTROLCHANGE, synthLib::MC_MODULATION, 0x7f});
				break;
			case '0':	
				// Modwheel Min
				mq.sendMidiEvent({synthLib::M_CONTROLCHANGE, synthLib::MC_MODULATION, 0x0});
				break;
			case '!':
				hw->getDSP(0).dumpPMem("dspA_dump_P_" + std::to_string(hw->getUcCycles()));
				if constexpr (mqLib::g_useVoiceExpansion)
				{
					hw->getDSP(1).dumpPMem("dspB_dump_P_" + std::to_string(hw->getUcCycles()));
					hw->getDSP(2).dumpPMem("dspC_dump_P_" + std::to_string(hw->getUcCycles()));
				}
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
	}
}
