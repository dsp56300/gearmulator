#include "sysexRemoteControl.h"

#include "hardwareLib/lcd.h"

#include "synthLib/midiTypes.h"

namespace jeLib
{
	enum class ButtonType;

	static constexpr uint8_t g_manufacturerId = 0x7d;	// 0x7D is explicitly reserved by the MIDI Manufacturers Association for "non-commercial/educational use"

	static constexpr uint8_t g_headerSize = 3;	// 0xf0, manufacturerId, command
	static constexpr uint8_t g_footerSize = 1;	// 0xf7

	void SysexRemoteControl::createSysexHeader(synthLib::SysexBuffer& _dst, uint8_t _cmd)
	{
		_dst.assign({0xf0, g_manufacturerId, _cmd});
	}

	void SysexRemoteControl::createSysexHeader(synthLib::SysexBuffer& _dst, CommandType _cmd)
	{
		createSysexHeader(_dst, static_cast<uint8_t>(_cmd));
	}

	void SysexRemoteControl::sendSysexLcdCgRam(std::vector<synthLib::SMidiEvent>& _dst, const hwLib::LCD& _lcd)
	{
		synthLib::SMidiEvent ev(synthLib::MidiEventSource::Internal);
		createSysexHeader(ev.sysex, CommandType::LcdCgRam);

		auto& data = _lcd.getCgRam();

		ev.sysex.reserve(data.size() << 1);

		for (const auto d : data)
		{
			assert(d <= 0xf0);
			ev.sysex.push_back(d);
		}

		ev.sysex.push_back(0xf7);

		_dst.emplace_back(ev);
	}

	void SysexRemoteControl::sendSysexLcdDdRam(std::vector<synthLib::SMidiEvent>& _dst, const hwLib::LCD& _lcd)
	{
		const auto& data = _lcd.getDdRam();

		synthLib::SMidiEvent ev(synthLib::MidiEventSource::Internal);

		createSysexHeader(ev.sysex, CommandType::LcdDdRam);

		for (const auto& c : data)
			ev.sysex.push_back(c);

		ev.sysex.push_back(0xf7);

		_dst.emplace_back(ev);
	}

	void SysexRemoteControl::sendSysexButton(std::vector<synthLib::SMidiEvent>& _dst, const uint32_t _buttonIndex, bool _pressed)
	{
		auto& ev = _dst.emplace_back(synthLib::MidiEventSource::Internal);

		createSysexHeader(ev.sysex, CommandType::Button);

		ev.sysex.push_back((_buttonIndex >> 12) & 0xf);
		ev.sysex.push_back((_buttonIndex >> 8) & 0xf);
		ev.sysex.push_back((_buttonIndex >> 4) & 0xf);
		ev.sysex.push_back(_buttonIndex & 0xf);

		ev.sysex.push_back(_pressed ? 1 : 0);

		ev.sysex.push_back(0xf7);
	}

	void SysexRemoteControl::sendSysexLeds(std::vector<synthLib::SMidiEvent>& _dst, const Leds& _ledStates)
	{
		auto& ev = _dst.emplace_back(synthLib::MidiEventSource::Internal);

		createSysexHeader(ev.sysex, CommandType::Leds);

		for(size_t i=0; i<_ledStates.size(); ++i)
			ev.sysex.push_back(_ledStates[i] ? 1 : 0);
			
		ev.sysex.push_back(0xf7);
	}

	void SysexRemoteControl::sendSysexParameter(std::vector<synthLib::SMidiEvent>& _dst, const uint8_t _page, const uint8_t _index, const int32_t& _value)
	{
		auto& ev = _dst.emplace_back(synthLib::MidiEventSource::Internal);

		createSysexHeader(ev.sysex, CommandType::SetParam);

		ev.sysex.push_back((_page >> 4) & 0xf);
		ev.sysex.push_back(_page & 0xf);
		ev.sysex.push_back((_index >> 4) & 0xf);
		ev.sysex.push_back(_index & 0xf);

		ev.sysex.push_back((_value >> 28) & 0xf);
		ev.sysex.push_back((_value >> 24) & 0xf);
		ev.sysex.push_back((_value >> 20) & 0xf);
		ev.sysex.push_back((_value >> 16) & 0xf);
		ev.sysex.push_back((_value >> 12) & 0xf);
		ev.sysex.push_back((_value >> 8) & 0xf);
		ev.sysex.push_back((_value >> 4) & 0xf);
		ev.sysex.push_back(_value & 0xf);

		ev.sysex.push_back(0xf7);
	}

	bool SysexRemoteControl::receive(const synthLib::SMidiEvent& _input)
	{
		if (_input.sysex.empty())
			return false;

		return receive(_input.sysex);
	}

	bool SysexRemoteControl::receive(const synthLib::SysexBuffer& _input)
	{
		if(_input.size() < g_headerSize + g_footerSize)
			return false;

		size_t i=1;

		if(_input[i++] != g_manufacturerId)
			return false;

		const auto cmd = _input[i++];

		switch (static_cast<CommandType>(cmd))
		{
		case CommandType::LcdCgRam:
			{
				std::array<uint8_t, 64> data;

				if(_input.size() != g_headerSize + g_footerSize + data.size()) // + data
					return false;

				for(auto& d : data)
					d = _input[i++];

				evLcdCgDataChanged.retain(data);
			}
			return true;
		case CommandType::LcdDdRam:
			{
				if(_input.size() != g_headerSize + g_footerSize + 40) // + 40 chars
					return false;
				std::array<char, 40> data;
				for(auto& c : data)
					c = static_cast<char>(_input[i++]);
				evLcdDdDataChanged.retain(data);
			}
			return true;
		case CommandType::Button:
			{
				if (_input.size() != g_headerSize + g_footerSize + 5) // + 4 nibbles + 1 pressed
					return false;
				uint32_t buttons = 0;
				for(uint32_t b=0; b<4; ++b)
				{
					const uint32_t n = _input[i++];
					buttons = (buttons<<4) | n;
				}
				const bool pressed = _input[i] ? true : false;
				evButtonChanged(buttons, pressed);
			}
			return true;
		case CommandType::Rotary:
			return true;

		case CommandType::SetParam:
			{
				if(_input.size() != g_headerSize + g_footerSize + 12) // + 12 nibbles
					return false;
				uint8_t paramPage = 0;
				for(uint32_t b=0; b<2; ++b)
				{
					const uint8_t n = _input[i++];
					paramPage = (paramPage<<4) | n;
				}
				uint8_t paramIndex = 0;
				for(uint32_t b=0; b<2; ++b)
				{
					const uint8_t n = _input[i++];
					paramIndex = (paramIndex<<4) | n;
				}
				int32_t paramValue = 0;
				for(uint32_t b=0; b<8; ++b)
				{
					const uint8_t n = _input[i++];
					paramValue = (paramValue<<4) | n;
				}

				evParamChanged(paramPage, paramIndex, paramValue);
			}
			return true;

		case CommandType::Leds:
			{
				if(_input.size() != g_headerSize + g_footerSize + std::tuple_size_v<Leds>) // + 66 nibbles
					return false;
				Leds ledStates{};
				for(size_t j=0; j<ledStates.size(); ++j)
				{
					ledStates[j] = _input[i++] != 0;
				}
				evLedsChanged(ledStates);
			}
			return true;

		default:
			return false;
		}
	}
}
