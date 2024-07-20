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

	using FrontPanelCS4 = FrontPanelCS<g_frontPanelAddressCS4>;
	using FrontPanelCS6 = FrontPanelCS<g_frontPanelAddressCS6>;

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
