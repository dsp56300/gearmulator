#include "xtPic.h"

#include "xtLcd.h"

#include "mc68k/mc68k.h"
#include "mc68k/logging.h"

#include <cassert>
#include <cstdint>

namespace xt
{
	Pic::Pic(mc68k::Mc68k& _uc, Lcd& _lcd)
	{
		_uc.getQSM().setSpiWriteCallback([&](const uint16_t _data, const uint8_t _index)->uint16_t
		{
			constexpr uint32_t transmitRamAddr = static_cast<uint32_t>(mc68k::PeriphAddress::TransmitRam0);
			constexpr uint32_t receiveRamAddr = static_cast<uint32_t>(mc68k::PeriphAddress::ReceiveRam0);
			constexpr uint32_t receiveRamSize = transmitRamAddr - receiveRamAddr;
			static_assert(receiveRamSize == 32);

			if(_index == 0)
			{
				// $30 enc 3 switches to multimode?
				// $35 = Osc 1 Semitone
				// $36 = Startwave1
				// $37 = Mix Wave1

				const uint8_t buttonA = m_spiButtons & 0x3f;
				const uint8_t buttonB = (m_spiButtons>>1) & 0x20;

				const uint16_t anus[] =
				{
					'M',		// don't care
					'W',		// don't care
					0x30,		// encoder address for encoders 1-4, range 0x30 - 0x57
					0,			// encoder value 1
					0,			// encoder value 2
					0,			// encoder value 3
					0,			// encoder value 4
					0x38,		// encoder address for encoders 5-8, range 0x30 - 0x57
					0,			// encoder value 5
					0,			// encoder value 6
					0,			// encoder value 7
					0,			// encoder value 8
					buttonA,	// Button flags A
					buttonB,	// Button flags B
					255,		// main volume pot
					'x'			// We are an XT
				};

				for(uint32_t a=0; a<std::size(anus); ++a)
					_uc.write16(a*2 + receiveRamAddr, anus[a]);
			}

			if(_index == 0)
			{
				m_picCommand0 = _data;
				return 0;
			}
			else if(_index == 1)
			{
				if(m_picCommand0 == 0x10)
				{
					if(_data == 0x80)
						_lcd.resetWritePos();

					return 0;
				}
			}

			// PIC Command 0x10 = new write, everything is for the LCD
			// Pic Command 0x18 = continue to write and update LEDs too

			assert(m_picCommand0 == 0x10 || m_picCommand0 == 0x18);

			uint8_t lcdChar = m_picCommand0 == 0x10 ? static_cast<uint8_t>(_data) : 0;

			if(m_picCommand0 == 0x18)
			{
				if(_index == 7)
					m_picHasLedUpdate = _data == 0x14;

				if(m_picHasLedUpdate)
				{
					switch (_index)
					{
					case 1:
					case 2:
					case 3:
					case 4:
					case 5:
					case 6:
						lcdChar = static_cast<uint8_t>(_data);
						break;
					case 8:
					case 9:
					case 10:
					case 11:
						{
							const auto oldLedState = m_ledState;
							m_ledState &= ~(0xff << ((_index-8) * 8));
							m_ledState |= (_data & 0xff) << ((_index-8) * 8);
							if(oldLedState != m_ledState)
							{
//								MCLOG("LEDs: " << MCHEXN(m_ledState, 4));
								m_cbkLedsDirty();
							}
						}
						break;
					default:
						return 0;
					}
				}
				else
				{
					lcdChar = static_cast<uint8_t>(_data);
				}
			}
			if(lcdChar)
			{
				const auto ch = static_cast<char>(lcdChar);

				if(_lcd.writeCharacter(ch))
				{
					m_cbkLcdDirty();
					MCLOG("LCD:\n" << _lcd.toString());
				}

				return 0;
			}
			return 0;
		});
	}

	Pic::~Pic()	= default;

	void Pic::setButton(const ButtonType _type, const bool _pressed)
	{
		const auto buttonMask = static_cast<uint8_t>(1 << static_cast<uint8_t>(_type));

		// inverted on purpose, pressed buttons are 0, released ones 1
		if(_pressed)
			m_spiButtons &= ~buttonMask;
		else
			m_spiButtons |= buttonMask;
	}

	bool Pic::getButton(ButtonType _button) const
	{
		const auto buttonMask = static_cast<uint8_t>(1 << static_cast<uint8_t>(_button));

		// inverted on purpose, pressed buttons are 0, released ones 1
		return (m_spiButtons & buttonMask) == 0;
	}

	void Pic::setLcdDirtyCallback(const DirtyCallback& _cbk)
	{
		m_cbkLcdDirty = _cbk;
		if(!m_cbkLcdDirty)
			m_cbkLcdDirty = [] {};
	}

	void Pic::setLedsDirtyCallback(const DirtyCallback& _cbk)
	{
		m_cbkLedsDirty = _cbk;
		if(!m_cbkLedsDirty)
			m_cbkLedsDirty = [] {};
	}

	bool Pic::getLedState(const LedType _led) const
	{
		return m_ledState & (1 << _led);
	}
}
