#include "n2xfrontpanel.h"

#include <cassert>
#include <cstring>	// memcpy

#include "n2xhardware.h"
#include "dsp56kEmu/logging.h"

namespace n2x
{
	template class FrontPanelCS<g_frontPanelAddressCS4>;
	template class FrontPanelCS<g_frontPanelAddressCS6>;

	template <uint32_t Base> FrontPanelCS<Base>::FrontPanelCS(FrontPanel& _fp): m_panel(_fp)
	{
	}

	FrontPanelCS4::FrontPanelCS4(FrontPanel& _fp) : FrontPanelCS(_fp)
	{
	}

	uint8_t FrontPanelCS4::read8(mc68k::PeriphAddress _addr)
	{
		const auto knobType = m_panel.cs6().getKnobType();

		switch (knobType)
		{
		case KnobType::Invalid:			return 0;
		case KnobType::PitchBend:		return 0;
		case KnobType::ModWheel:		return 0;			// pretend we're a rack unit
		case KnobType::MasterVol:		return 0xff;
		case KnobType::AmpGain:			return 0xff;

		case KnobType::Osc1Fm:			return 0;
		case KnobType::Porta:			return 0;
		case KnobType::Lfo2Rate:		return 0x0;
		case KnobType::Lfo1Rate:		return 0x0;
		case KnobType::ModEnvAmt:		return 0;
		case KnobType::ModEnvD:			return 0;
		case KnobType::ModEnvA:			return 0;
		case KnobType::AmpEnvD:			return 0;
		case KnobType::FilterFreq:		return 0xff;
		case KnobType::FilterEnvA:		return 0;
		case KnobType::AmpEnvA:			return 0;
		case KnobType::OscMix:			return 0x7f;
		case KnobType::Osc2Fine:		return 0x7f;
		case KnobType::Lfo1Amount:		return 0x0f;
		case KnobType::OscPW:			return 0x40;
		case KnobType::FilterEnvR:		return 0x30;
		case KnobType::AmpEnvR:			return 0x90;
		case KnobType::FilterEnvAmt:	return 0;
		case KnobType::FilterEnvS:		return 0x7f;
		case KnobType::AmpEnvS:			return 0x7f;
		case KnobType::FilterReso:		return 0x10;
		case KnobType::FilterEnvD:		return 0;
		case KnobType::ExpPedal:		return 0x0;
		case KnobType::Lfo2Amount:		return 0;
		case KnobType::Osc2Semi:		return 0x7f;
		default:
			assert(false);
			return 0x80;
		}
	}

	FrontPanelCS6::FrontPanelCS6(FrontPanel& _fp) : FrontPanelCS(_fp), m_buttonStates({})
	{
		m_buttonStates.fill(0xff);
	}

	void FrontPanelCS6::write8(const mc68k::PeriphAddress _addr, const uint8_t _val)
	{
//		LOG("Write CS6 " << HEXN(_addr - base(),2) << " = " << HEXN(_val,2));

		switch (static_cast<uint32_t>(_addr))
		{
			case g_frontPanelAddressCS6 + 0x8:
				m_ledLatch8 = _val;
				break;
			case g_frontPanelAddressCS6 + 0xa:
				m_ledLatch10 = _val;

//				LOG("Read pot " << HEXN((_val & 0x7f), 2));
				m_selectedKnob = static_cast<KnobType>(_val & 0x7f);

				if(m_ledLatch10 & (1<<7))
				{
					m_lcds[2] = m_ledLatch8;
					onLCDChanged();
				}
				break;
			case g_frontPanelAddressCS6 + 0xc:
				{
					bool gotLCDs = false;

					m_ledLatch12 = _val;

					if(m_ledLatch12 & (1<<6))
					{
						m_lcds[0] = m_ledLatch8;
						gotLCDs = true;
					}
					if(m_ledLatch12 & (1<<7))
					{
						m_lcds[1] = m_ledLatch8;
						gotLCDs = true;
					}

					if(gotLCDs)
						onLCDChanged();
				}
				break;
		}
		FrontPanelCS::write8(_addr, _val);
	}

	uint8_t FrontPanelCS6::read8(mc68k::PeriphAddress _addr)
	{
		const auto a = static_cast<uint32_t>(_addr);
		switch (a)
		{
		case g_frontPanelAddressCS6:		return m_buttonStates[0];
		case g_frontPanelAddressCS6 + 2:	return m_buttonStates[1];
		case g_frontPanelAddressCS6 + 4:	return m_buttonStates[2];
		case g_frontPanelAddressCS6 + 6:	return m_buttonStates[3];
		}
		return FrontPanelCS::read8(_addr);
	}

	void FrontPanelCS6::setButtonState(ButtonType _button, const bool _pressed)
	{
		const auto id = static_cast<uint32_t>(_button);
		const auto index = id>>9;
		const auto mask = id & 0xff;
		if(_pressed)
			m_buttonStates[index] |= mask;
		else
			m_buttonStates[index] &= ~mask;
	}

	bool FrontPanelCS6::getButtonState(ButtonType _button) const
	{
		const auto id = static_cast<uint32_t>(_button);
		const auto index = id>>9;
		const auto mask = id & 0xff;
		return m_buttonStates[index] & mask ? false : true;
	}

	void FrontPanelCS6::printLCD() const
	{
		static uint32_t count = 0;
		++count;
		if(count & 0xff)
			return;
		/*
		 --    --    --
		|  |  |  |  |  |
		 --    --    --
		|  |  |  |  |  |
		 --    --    --
		*/

		using Line = std::array<char, 16>;
		std::array<Line,5> buf;

		for (auto& b : buf)
		{
			b.fill(' ');
		}

		int off = 0;

		auto drawH = [&](int _x, const int _y, const bool _set)
		{
			_x += off;
			buf[_y][_x  ] = _set ? '-' : ' ';
			buf[_y][_x+1] = _set ? '-' : ' ';
		};

		auto drawV = [&](int _x, const int _y, const bool _set)
		{
			_x += off;
			buf[_y][_x] = _set ? '|' : ' ';
		};

		for(auto i=0; i<3; ++i)
		{
			auto bt = [&](const uint32_t _bit)
			{
				return !(m_lcds[i] & (1<<_bit));
			};

			drawH(1, 0, bt(7));
			drawH(1, 2, bt(1));
			drawH(1, 4, bt(4));

			drawV(0, 1, bt(2));
			drawV(3, 1, bt(6));
			drawV(0, 3, bt(3));
			drawV(3, 3, bt(5));

			off += 6;
		}

		buf[4][4] = (m_lcds[0] & (1<<0)) ? ' ' : '*';

		char message[(sizeof(Line) + 1) * buf.size() + 1];
		size_t i=0;
		for (const auto& line : buf)
		{
			memcpy(&message[i], line.data(), line.size());
			i += line.size();
			message[i++] = '\n';
		}
		assert(i == std::size(message)-1);
		message[i] = 0;

		LOG("LCD:\n" << message);
	}

	void FrontPanelCS6::onLCDChanged()
	{
		// Check if the LCD display "  1", used as indication that device has finished booting
		if(m_lcds[0] == 255 && m_lcds[1] >= 254 && m_lcds[2] == 159)
		{
			m_panel.getHardware().notifyBootFinished();
		}
		printLCD();
	}

	FrontPanel::FrontPanel(Hardware& _hardware) : m_hardware(_hardware), m_cs4(*this), m_cs6(*this)
	{
	}
}
