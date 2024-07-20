#include "n2xfrontpanel.h"

#include <cassert>

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

	FrontPanelCS6::FrontPanelCS6(FrontPanel& _fp) : FrontPanelCS(_fp)
	{
	}

	void FrontPanelCS6::write8(const mc68k::PeriphAddress _addr, const uint8_t _val)
	{
		switch (static_cast<uint32_t>(_addr))
		{
			case g_frontPanelAddressCS6 + 0x8:
				{
					m_ledLatch8 = _val;

					// 10 / 12 first and then 8 didn't lead to any result

					/*
					bool gotLCDs = false;
					if(m_ledLatch12 & (1<<6))
					{
						m_lcds[0] = _val;
						gotLCDs = true;
					}
					if(m_ledLatch12 & (1<<7))
					{
						m_lcds[1] = _val;
						gotLCDs = true;
					}
					if(m_ledLatch10 & (1<<7))
					{
						m_lcds[2] = _val;
						gotLCDs = true;
					}

					if(gotLCDs)
						printLCD();
					*/
				}
				break;
			case g_frontPanelAddressCS6 + 0xa:
				m_ledLatch10 = _val;
				if(m_ledLatch10 & (1<<7))
				{
					m_lcds[2] = m_ledLatch8;
					printLCD();
				}
				break;
			case g_frontPanelAddressCS6 + 0xc:
				{
					bool gotLCDs = false;

					m_ledLatch12 = _val;

					if(m_ledLatch12 & (1<<6))
					{
						m_lcds[0] = _val;
						gotLCDs = true;
					}
					if(m_ledLatch12 & (1<<7))
					{
						m_lcds[1] = _val;
						gotLCDs = true;
					}

					if(gotLCDs)
						printLCD();
				}
				break;
		}
		FrontPanelCS<2105344>::write8(_addr, _val);
	}

	void FrontPanelCS6::printLCD()
	{
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
			drawH(1, 0, m_lcds[i] & (1<<1));
			drawH(1, 2, m_lcds[i] & (1<<7));
			drawH(1, 4, m_lcds[i] & (1<<4));

			drawV(0, 1, m_lcds[i] & (1<<2));
			drawV(3, 1, m_lcds[i] & (1<<6));
			drawV(0, 3, m_lcds[i] & (1<<3));
			drawV(3, 3, m_lcds[i] & (1<<5));

			off += 6;
		}

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

	FrontPanel::FrontPanel(): m_cs4(*this), m_cs6(*this)
	{
	}
}
