#pragma once

#include "xtButtons.h"
#include "xtLcd.h"
#include "xtLeds.h"
#include "xtPic.h"
#include "xtTypes.h"

#include "mc68k/mc68k.h"
#include "mc68k/hdi08periph.h"

#include "hardwareLib/am29f.h"

namespace xt
{
	class Rom;

	using xtHdi08A = mc68k::Hdi08Periph<0xfe000>;

	class XtUc : public mc68k::Mc68k
	{
	public:
		XtUc(const Rom& _rom);
		uint32_t exec() override;

		uint16_t readImm16(uint32_t _addr) override;
		uint16_t read16(uint32_t _addr) override;
		uint8_t read8(uint32_t _addr) override;

		void write16(uint32_t _addr, uint16_t _val) override;
		void write8(uint32_t _addr, uint8_t _val) override;

		xtHdi08A& getHdi08A() { return m_hdiA; }

		bool requestDSPReset() const { return m_dspResetRequest; }
		void notifyDSPBooted() { m_dspResetCompleted = true; }

		void onPortQSWritten();

		void setButton(ButtonType _type, bool _pressed);

		void setLcdDirtyCallback(const Pic::DirtyCallback& _callback);
		void setLedsDirtyCallback(const Pic::DirtyCallback& _callback);

		Lcd& getLcd() { return m_lcd; }
		bool getLedState(LedType _led) const;
		bool getButton(ButtonType _button) const;

	private:
		std::array<uint8_t, g_ramSize> m_memory;
		std::array<uint8_t, g_romSize> m_romRuntimeData;

		xtHdi08A m_hdiA;
		hwLib::Am29f m_flash;
		Pic m_pic;
		Lcd m_lcd;

		bool m_dspResetRequest = false;
		bool m_dspResetCompleted = false;
	};
}
