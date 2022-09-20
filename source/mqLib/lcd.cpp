#include "lcd.h"

#include "mqtypes.h"

#include <cassert>

#include "../mc68k/port.h"

#include "dsp56kEmu/logging.h"

namespace mqLib
{
	LCD::LCD() = default;

	bool LCD::exec(mc68k::Port& _portGp, mc68k::Port& _portF)
	{
		if(_portF.getWriteCounter() == m_lastWriteCounter)
			return false;

		m_lastWriteCounter = _portF.getWriteCounter();

		const auto f = _portF.read();
		const auto g = _portGp.read();
		const auto df = _portF.getDirection();
		//const auto dg = _portGp.getDirection();

		const auto registerSelect = (f>>LcdRS) & 1;
		const auto read = (f>>LcdRW) & 1;
		const auto opEnable = ((f & df)>>LcdLatch) & 1;

		// falling edge triggered
		const auto execute = m_lastOpState && !opEnable;

		m_lastOpState = opEnable;

		if(!execute)
			return false;

		bool changed = false;

		if(!read)
		{
			if(!registerSelect)
			{
				if(g == 0x01)
				{
					LOG("LCD Clear Display");
					m_dramData.fill(' ');
					changed = true;
				}
				else if(g == 0x02)
				{
					LOG("LCD Return Home");
					m_dramAddr = 0;
					m_cursorPos = 0;
				}
				else if((g & 0xfc) == 0x04)
				{
					const int increment = (g >> 1) & 1;
					const int shift = g & 1;
					LOG("LCD Entry Mode Set, inc=" << increment << ", shift=" << shift);

					m_addrIncrement = increment ? 1 : -1;
				}
				else if((g & 0xf8) == 0x08)
				{
					const int displayOnOff = (g >> 2) & 1;
					const int cursorOnOff = (g >> 1) & 1;
					const int cursorBlinking = g & 1;

					LOG("LCD Display ON/OFF, display=" << displayOnOff << ", cursor=" << cursorOnOff << ", blinking=" << cursorBlinking);

					m_displayOn = displayOnOff != 0;
					m_cursorOn = cursorOnOff != 0;
					m_cursorBlinking = cursorBlinking != 0;
				}
				else if((g & 0xf3) == 0x10)
				{
					const int scrl = (g >> 2) & 3;

					LOG("LCD Cursor/Display Shift, scrl=" << scrl);
					m_cursorShift = static_cast<CursorShiftMode>(scrl);
				}
				else if((g & 0xec) == 0x28)
				{
					const int dl = (g >> 4) & 1;
					const int ft = g & 3;

					LOG("LCD Function Set, dl=" << dl << ", ft=" << ft);
					m_dataLength = static_cast<DataLength>(dl);
					m_fontTable = static_cast<FontTable>(ft);
				}
				else if(g & (1<<7))
				{
					const int addr = g & 0x7f;
//					LOG("LCD Set DDRAM address, addr=" << addr);
					m_dramAddr = addr;
					m_addressMode = AddressMode::DDRam;
				}
				else if(g & (1<<6))
				{
					const int acg = g & 0x3f;

//					LOG("LCD Set CGRAM address, acg=" << acg);
					m_cgramAddr = acg;
					m_addressMode = AddressMode::CGRam;
				}
				else
				{
					LOG("LCD unknown command");
					assert(false);
				}
			}
			else
			{
				if(m_addressMode == AddressMode::CGRam)
				{
//					changed = true;
//					LOG("LCD write data to CGRAM addr " << m_cgramAddr << ", data=" << static_cast<int>(g));

					m_cgramData[m_cgramAddr] = g;
					m_cgramAddr += m_addrIncrement;

					if((false && (m_cgramAddr & 0x7) == 0))
					{
						std::stringstream ss;
						ss << "CG RAM character " << (m_cgramAddr/8 - 1) << ':' << std::endl;
						ss << "##################" << std::endl;
						for(auto i = m_cgramAddr - 8; i < m_cgramAddr - 1; ++i)
						{
							ss << '#';
							for(int x=7; x >= 0; --x)
							{
								if(m_cgramData[i] & (1<<x))
									ss << "[]";
								else
									ss << "  ";
							}
							ss << '#' << std::endl;
						}
						ss << "##################" << std::endl;
						const auto s(ss.str());
						LOG(s);
					}
				}
				else
				{
//					LOG("LCD write data to DDRAM addr " << m_dramAddr << ", data=" << static_cast<int>(g) << ", char=" << static_cast<char>(g));

					const auto old = m_dramData;

					if(m_dramAddr >= 20 && m_dramAddr < 0x40)
					{
						for(size_t i=1; i<=20; ++i)
							m_dramData[i-1] = m_dramData[i];
						m_dramData[19] = g;
					}
					else if(m_dramAddr > 0x53)
					{
						for(size_t i=21; i<=40; ++i)
							m_dramData[i-1] = m_dramData[i];

						m_dramData[39] = g;
					}
					else
					{
						if(m_dramAddr < 20)
							m_dramData[m_dramAddr] = g;
						else
							m_dramData[m_dramAddr - 0x40 + 20] = g;
					}

					if(m_dramAddr != 20 && m_dramAddr != 0x54)
						m_dramAddr += m_addrIncrement;

					if(m_dramData != old)
						changed = true;
				}
			}
		}
		else
		{
			if(registerSelect)
			{
				LOG("LCD read data from CGRAM or DDRAM");
				if(m_addressMode == AddressMode::CGRam)
					_portGp.writeRX(m_cgramData[m_cgramAddr]);
				else
					_portGp.writeRX(m_dramData[m_dramAddr]);
			}
			else
			{
				LOG("LCD read busy flag & address");
				if(m_addressMode == AddressMode::CGRam)
				{
					_portGp.writeRX(static_cast<uint8_t>(m_cgramAddr));
				}
				else
				{
					auto a = m_dramAddr;
					if(a > 0x53)
						a = 0x53;
					if(a == 20)
						a = 19;
					_portGp.writeRX(static_cast<uint8_t>(m_dramAddr));
				}
			}
		}

		if(changed && m_changeCallback)
			m_changeCallback();

		return true;
	}

	bool LCD::getCgData(std::array<uint8_t, 8>& _data, uint32_t _charIndex) const
	{
		const auto idx = _charIndex * 8;
		if(idx + 8 >= getCgRam().size())
			return false;

		uint32_t j = 0;

		for(auto i = idx; i<idx+8; ++i)
			_data[j++] = getCgRam()[i];

		return true;
	}
}
