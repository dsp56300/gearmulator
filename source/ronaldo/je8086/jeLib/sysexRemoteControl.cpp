#include "sysexRemoteControl.h"

namespace jeLib
{
	void SysexRemoteControl::sendSysexLcdCgRam(std::vector<synthLib::SMidiEvent>& _dst, const hwLib::Hd44780& _lcd)
	{
		auto& s = begin(_dst, CommandType::LcdCgRam);
		pushLcdCgRam(s, _lcd);
		end(s);
	}

	void SysexRemoteControl::sendSysexLcdDdRam(std::vector<synthLib::SMidiEvent>& _dst, const hwLib::Hd44780& _lcd)
	{
		auto& s = begin(_dst, CommandType::LcdDdRam);
		pushLcdVisibleDdRam(s, _lcd);
		end(s);
	}

	void SysexRemoteControl::sendSysexButton(std::vector<synthLib::SMidiEvent>& _dst, const uint32_t _buttonIndex, const bool _pressed)
	{
		auto& s = begin(_dst, CommandType::Button);
		pushNibbles(s, _buttonIndex, 4);
		s.push_back(_pressed ? 1 : 0);
		end(s);
	}

	void SysexRemoteControl::sendSysexParameter(std::vector<synthLib::SMidiEvent>& _dst, const uint8_t _page, const uint8_t _index, const int32_t _value)
	{
		auto& s = begin(_dst, CommandType::SetParam);
		pushNibbles(s, _page, 2);
		pushNibbles(s, _index, 2);
		pushNibbles(s, static_cast<uint32_t>(_value), 8);
		end(s);
	}

	bool SysexRemoteControl::onCommand(const uint8_t _command, const uint8_t* _payload, const size_t _size) const
	{
		switch (static_cast<CommandType>(_command))
		{
		case CommandType::LcdCgRam:
			if(_size != CgRamSize)
				return false;
			evLcdCgDataChanged.retain(readArray<uint8_t, CgRamSize>(_payload));
			return true;
		case CommandType::LcdDdRam:
			if(_size != VisibleDdRamSize)
				return false;
			evLcdDdDataChanged.retain(readArray<char, VisibleDdRamSize>(_payload));
			return true;
		case CommandType::Button:
			if(_size != 5)
				return false;
			evButtonChanged(static_cast<uint32_t>(readNibbles(_payload, 4)), _payload[4] != 0);
			return true;
		case CommandType::SetParam:
			if(_size != 12)
				return false;
			evParamChanged(static_cast<uint8_t>(readNibbles(_payload, 2)), static_cast<uint8_t>(readNibbles(_payload + 2, 2)),
				static_cast<int32_t>(static_cast<uint32_t>(readNibbles(_payload + 4, 8))));
			return true;
		case CommandType::Rotary:
		case CommandType::Leds:
			return true;
		default:
			return false;
		}
	}
}
