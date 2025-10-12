#pragma once

#include <vector>
#include <array>

#include "baseLib/event.h"

namespace hwLib
{
	class LCD;
}

namespace synthLib
{
	struct SMidiEvent;
}

namespace jeLib
{
	// TODO: refactor this and create a common class for JE/SXX
	class SysexRemoteControl
	{
	public:
		using Leds = std::array<bool, 66>;

		baseLib::Event<std::array<uint8_t ,64>> evLcdCgDataChanged;
		baseLib::Event<std::array<char, 40>> evLcdDdDataChanged;
		baseLib::Event<Leds> evLedsChanged;
		baseLib::Event<uint16_t> evButtonsChanged;
		baseLib::Event<uint8_t, uint8_t, int32_t> evParamChanged; // param page, param index, value

		enum class CommandType : uint8_t
		{
			LcdCgRam,		// device => UI
			LcdDdRam,		// device => UI
			Buttons,		// UI => device
			Rotary,			// UI => device
			Leds,			// device => UI
			SetParam
		};

		static void createSysexHeader(std::vector<uint8_t>& _dst, uint8_t _cmd);
		static void createSysexHeader(std::vector<uint8_t>& _dst, CommandType _cmd);

		static void sendSysexLcdCgRam(std::vector<synthLib::SMidiEvent>& _dst, const hwLib::LCD& _lcd);
		static void sendSysexLcdDdRam(std::vector<synthLib::SMidiEvent>& _dst, const hwLib::LCD& _lcd);

		static void sendSysexButtons(std::vector<synthLib::SMidiEvent>& _dst, uint16_t _buttonStates);
		static void sendSysexLeds(std::vector<synthLib::SMidiEvent>& _dst, const Leds& _ledStates);
		static void sendSysexParameter(std::vector<synthLib::SMidiEvent>& _dst, uint8_t _page, uint8_t _index, const int32_t& _value);

		bool receive(const synthLib::SMidiEvent& _input);
		bool receive(const std::vector<uint8_t>& _input);
	private:
	};
}
