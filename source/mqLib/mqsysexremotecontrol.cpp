#include "mqsysexremotecontrol.h"

#include "buttons.h"
#include "mqmiditypes.h"
#include "microq.h"

#include "../synthLib/midiTypes.h"

namespace mqLib
{
	void SysexRemoteControl::createSysexHeader(std::vector<uint8_t>& _dst, SysexCommand _cmd)
	{
		constexpr uint8_t devId = 0;
		_dst.assign({0xf0, IdWaldorf, IdMicroQ, devId, static_cast<uint8_t>(_cmd)});
	}

	void SysexRemoteControl::sendSysexLCD(std::vector<synthLib::SMidiEvent>& _dst) const
	{
		std::array<char, 40> lcdData{};
		m_mq.readLCD(lcdData);

		synthLib::SMidiEvent ev;
		createSysexHeader(ev.sysex, SysexCommand::EmuLCD);
		ev.sysex.insert(ev.sysex.end(), lcdData.begin(), lcdData.end());

		_dst.emplace_back(ev);
	}
	
	void SysexRemoteControl::sendSysexLCDCGRam(std::vector<synthLib::SMidiEvent>& _dst) const
	{
		std::array<char, 64> lcdData{};

		std::array<uint8_t, 8> data{};

		for (auto i=0, k=0; i<8; ++i)
		{
			m_mq.readCustomLCDCharacter(data, i);
			for (auto j=0; j<8; ++j)
				lcdData[k++] = static_cast<char>(data[j]);
		}

		synthLib::SMidiEvent ev;
		createSysexHeader(ev.sysex, SysexCommand::EmuLCDCGRata);
		ev.sysex.insert(ev.sysex.end(), lcdData.begin(), lcdData.end());

		_dst.emplace_back(ev);
	}
	
	void SysexRemoteControl::sendSysexButtons(std::vector<synthLib::SMidiEvent>& _dst) const
	{
		static_assert(static_cast<uint32_t>(Buttons::ButtonType::Count) < 24, "too many buttons");
		uint32_t buttons = 0;
		for(uint32_t i=0; i<static_cast<uint32_t>(Buttons::ButtonType::Count); ++i)
		{
			if(m_mq.getButton(static_cast<Buttons::ButtonType>(i)))
				buttons |= (1<<i);
		}

		auto& ev = _dst.emplace_back();

		createSysexHeader(ev.sysex, SysexCommand::EmuButtons);

		ev.sysex.push_back((buttons>>16) & 0xff);
		ev.sysex.push_back((buttons>>8) & 0xff);
		ev.sysex.push_back(buttons & 0xff);
	}

	void SysexRemoteControl::sendSysexLEDs(std::vector<synthLib::SMidiEvent>& _dst) const
	{
		static_assert(static_cast<uint32_t>(Leds::Led::Count) < 32, "too many LEDs");
		uint32_t leds = 0;
		for(uint32_t i=0; i<static_cast<uint32_t>(Leds::Led::Count); ++i)
		{
			if(m_mq.getLedState(static_cast<Leds::Led>(i)))
				leds |= (1<<i);
		}
		auto& ev = _dst.emplace_back();
		auto& response = ev.sysex;
		createSysexHeader(response, SysexCommand::EmuLEDs);
		response.push_back((leds>>24) & 0xff);
		response.push_back((leds>>16) & 0xff);
		response.push_back((leds>>8) & 0xff);
		response.push_back(leds & 0xff);
	}

	void SysexRemoteControl::sendSysexRotaries(std::vector<synthLib::SMidiEvent>& _dst) const
	{
		auto& ev= _dst.emplace_back();
		auto& response = ev.sysex;

		createSysexHeader(response, SysexCommand::EmuRotaries);

		for(uint32_t i=0; i<static_cast<uint32_t>(Buttons::Encoders::Count); ++i)
		{
			const auto value = m_mq.getEncoder(static_cast<Buttons::Encoders>(i));
			response.push_back(value);
		}
	}

	bool SysexRemoteControl::receive(std::vector<synthLib::SMidiEvent>& _output, const std::vector<unsigned char>& _input) const
	{
		if(_input.size() < 5)
			return false;

		if(_input[1] != IdWaldorf || _input[2] != IdMicroQ)
			return false;

		const auto cmd = _input[4];

		switch (static_cast<SysexCommand>(cmd))
		{
		case SysexCommand::EmuLCD:
			sendSysexLCD(_output);
			return true;
		case SysexCommand::EmuLCDCGRata:
			sendSysexLCDCGRam(_output);
			return true;
		case SysexCommand::EmuButtons:
			{
				if(_input.size() > 6)
				{
					const auto button = static_cast<Buttons::ButtonType>(_input[5]);
					const auto state = _input[6];
					m_mq.setButton(button, state != 0);
				}
				else
				{
					sendSysexButtons(_output);
				}
			}
			return true;
		case SysexCommand::EmuLEDs:
			{
				sendSysexLEDs(_output);
			}
			return true;
		case SysexCommand::EmuRotaries:
			{
				if(_input.size() > 6)
				{
					const auto encoder = static_cast<Buttons::Encoders>(_input[5]);
					const auto amount = static_cast<int>(_input[6]) - 64;
					if(amount)
						m_mq.rotateEncoder(encoder, amount);
				}
				else
				{
					sendSysexRotaries(_output);
				}
			}
			return true;
		default:
			return false;
		}
	}

	void SysexRemoteControl::handleDirtyFlags(std::vector<synthLib::SMidiEvent>& _output, uint32_t _dirtyFlags) const
	{
		if(_dirtyFlags & static_cast<uint32_t>(MicroQ::DirtyFlags::Lcd))
			sendSysexLCD(_output);
		if(_dirtyFlags & static_cast<uint32_t>(MicroQ::DirtyFlags::LcdCgRam))
			sendSysexLCDCGRam(_output);
		if(_dirtyFlags & static_cast<uint32_t>(MicroQ::DirtyFlags::Leds))
			sendSysexLEDs(_output);
	}
}
