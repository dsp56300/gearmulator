#pragma once

#include <list>
#include <memory>

#include "buttons.h"
#include "lcd.h"
#include "leds.h"
#include "am29f.h"

#include "../mc68k/mc68k.h"

namespace mqLib
{
	class ROM;

	class MqMc final : public mc68k::Mc68k
	{
	public:
		explicit MqMc(const ROM& _rom);
		~MqMc() override;

		uint32_t exec() override;

		Buttons& getButtons() { return m_buttons; }
		Leds& getLeds() { return m_leds; }
		LCD& getLcd() { return m_lcd; }

		bool requestDSPReset() const { return m_dspResetRequest; }
		void notifyDSPBooted();

		uint8_t requestDSPinjectNMI() const { return m_dspInjectNmiRequest; }

		void dumpMemory(const char* _filename) const;
		void dumpROM(const char* _filename) const;
		void dumpAssembly(uint32_t _first, uint32_t _count);

	private:
		uint16_t readImm16(uint32_t _addr) override;
		uint16_t read16(uint32_t addr) override;
		uint8_t read8(uint32_t addr) override;
		void write16(uint32_t addr, uint16_t val) override;
		void write8(uint32_t addr, uint8_t val) override;

		void onReset() override;
		uint32_t onIllegalInstruction(uint32_t opcode) override;

		const ROM& m_rom;
		std::vector<uint8_t> m_romRuntimeData;
		std::unique_ptr<Am29f> m_flash;
		LCD m_lcd;
		Buttons m_buttons;
		Leds m_leds;
		std::vector<uint8_t> m_memory;
		std::list<uint32_t> m_lastPCs;
		bool m_dspResetRequest = false;
		bool m_dspResetCompleted = false;
		uint8_t m_dspInjectNmiRequest = 0;
	};
}
