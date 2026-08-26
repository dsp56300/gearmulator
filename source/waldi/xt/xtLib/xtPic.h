#pragma once

#include <cstdint>
#include <functional>

#include "xtButtons.h"
#include "xtLeds.h"

namespace mc68k
{
	class Mc68k;
}

namespace xt
{
	class Lcd;

	class Pic
	{
	public:
		using DirtyCallback = std::function<void()>;

		Pic(mc68k::Mc68k& _uc, Lcd& _lcd);
		~Pic();

		void setButton(ButtonType _type, bool _pressed);
		bool getButton(ButtonType _button) const;

		void setLcdDirtyCallback(const DirtyCallback& _cbk);
		void setLedsDirtyCallback(const DirtyCallback& _cbk);
		bool getLedState(LedType _led) const;

	private:
		uint16_t m_picCommand0 = 0;
		bool m_picHasLedUpdate = false;

		uint8_t m_spiButtons = 0xff;
		uint32_t m_ledState = 0;
		DirtyCallback m_cbkLedsDirty = [] {};
		DirtyCallback m_cbkLcdDirty = [] {};
	};
}
