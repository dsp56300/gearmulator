#pragma once

#include "n2xfrontpanel.h"
#include "n2xi2cflash.h"
#include "n2xtypes.h"
#include "mc68k/hdi08.h"
#include "mc68k/hdi08periph.h"
#include "mc68k/mc68k.h"
#include "wLib/wMidi.h"

namespace n2x
{
	class Rom;

	using Hdi08DspA = mc68k::Hdi08Periph<g_dspAAddress>;
	using Hdi08DspB = mc68k::Hdi08Periph<g_dspBAddress>;

	class Microcontroller : public mc68k::Mc68k
	{
	public:
		explicit Microcontroller(const Rom& _rom);

		auto& getHdi08A() { return m_hdi08A.getHdi08(); }
		auto& getHdi08B() { return m_hdi08B.getHdi08(); }
		auto& getMidi() const	{ return m_midi; }

		uint32_t exec() override;

		auto getPrevPC() const { return m_prevPC; }

	private:
		uint32_t read32(uint32_t _addr) override;
		uint16_t readImm16(uint32_t _addr) override;
		uint16_t read16(uint32_t _addr) override;
		uint8_t read8(uint32_t _addr) override;

		void write16(uint32_t _addr, uint16_t _val) override;
		void write8(uint32_t _addr, uint8_t _val) override;

		std::array<uint8_t, g_romSize> m_rom;
		std::array<uint8_t, g_ramSize> m_ram;
		I2cFlash m_flash;

		Hdi08DspA m_hdi08A;
		Hdi08DspB m_hdi08B;
		FrontPanel m_panel;

		uint32_t m_prevPC;
		wLib::Midi m_midi;
		uint64_t m_totalCycles = 0;
		bool m_hasSentMidi = false;
	};
}