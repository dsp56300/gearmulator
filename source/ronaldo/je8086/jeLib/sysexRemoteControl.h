#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "baseLib/event.h"

#include "common/sysexRemoteControl.h"

namespace jeLib
{
	class SysexRemoteControl : public rLib::SysexRemoteControl
	{
	public:
		enum class CommandType : uint8_t
		{
			LcdCgRam = 0,	// device => UI
			LcdDdRam = 1,	// device => UI
			Button = 2,		// UI => device
			Rotary = 3,		// retired, still consumed
			Leds = 4,		// retired, still consumed
			SetParam = 5
		};

		// retained: an LCD widget created later still gets the current content
		mutable baseLib::Event<std::array<uint8_t, CgRamSize>> evLcdCgDataChanged;
		mutable baseLib::Event<std::array<char, VisibleDdRamSize>> evLcdDdDataChanged;
		baseLib::Event<uint32_t, bool> evButtonChanged;
		baseLib::Event<uint8_t, uint8_t, int32_t> evParamChanged; // param page, param index, value

		static void sendSysexLcdCgRam(std::vector<synthLib::SMidiEvent>& _dst, const hwLib::Hd44780& _lcd);
		static void sendSysexLcdDdRam(std::vector<synthLib::SMidiEvent>& _dst, const hwLib::Hd44780& _lcd);
		static void sendSysexButton(std::vector<synthLib::SMidiEvent>& _dst, uint32_t _buttonIndex, bool _pressed);
		static void sendSysexParameter(std::vector<synthLib::SMidiEvent>& _dst, uint8_t _page, uint8_t _index, int32_t _value);

	protected:
		bool onCommand(uint8_t _command, const uint8_t* _payload, size_t _size) const override;
	};
}
