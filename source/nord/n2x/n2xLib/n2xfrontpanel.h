#pragma once

#include "n2xtypes.h"
#include "mc68k/peripheralBase.h"

namespace n2x
{
	class FrontPanel;

	template<uint32_t Base>
	class FrontPanelCS : public mc68k::PeripheralBase<Base, g_frontPanelSize>
	{
	public:
		explicit FrontPanelCS(FrontPanel& _fp);

	private:
		FrontPanel& m_panel;
	};

	class FrontPanelCS4 : public FrontPanelCS<g_frontPanelAddressCS4>
	{
	public:
		explicit FrontPanelCS4(FrontPanel& _fp);
	};

	class FrontPanelCS6 : public FrontPanelCS<g_frontPanelAddressCS6>
	{
	public:
		explicit FrontPanelCS6(FrontPanel& _fp);

		void write8(mc68k::PeriphAddress _addr, uint8_t _val) override;

	private:
		void printLCD();

		uint8_t m_ledLatch8 = 0;
		uint8_t m_ledLatch10 = 0;
		uint8_t m_ledLatch12 = 0;
		std::array<uint8_t,3> m_lcds{0,0,0};
		std::array<uint8_t,3> m_lcdsPrev{0,0,0};
	};

	class FrontPanel
	{
	public:
		FrontPanel();

		auto& cs4() { return m_cs4; }
		auto& cs6() { return m_cs6; }

		bool isInRange(const mc68k::PeriphAddress _pa) const
		{
			return m_cs4.isInRange(_pa) || m_cs6.isInRange(_pa);
		}

	private:
		FrontPanelCS4 m_cs4;
		FrontPanelCS6 m_cs6;
	};
}
