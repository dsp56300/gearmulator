#pragma once

#include <list>

#include "buttons.h"
#include "lcd.h"

#include "../68kEmu/mc68k.h"

namespace mqLib
{
	class ROM;

	class MqMc : public mc68k::Mc68k
	{
	public:
		explicit MqMc(ROM& _rom);
		~MqMc() override;

		void exec() override;

		Buttons& getButtons() { return m_buttons; }

		bool requestDSPReset() const { return m_dspResetRequest; }
		void notifyDSPBooted();

		uint8_t requestDSPinjectNMI() const { return m_dspInjectNmiRequest; }

	private:
		uint16_t read16(uint32_t addr) override;
		uint8_t read8(uint32_t addr) override;
		void write16(uint32_t addr, uint16_t val) override;
		void write8(uint32_t addr, uint8_t val) override;

		void dumpMemory(const char* _filename) const;
		void dumpAssembly(uint32_t _first, uint32_t _count) const;

		void onReset() override;
		uint32_t onIllegalInstruction(uint32_t opcode) override;

		ROM& m_rom;
		LCD m_lcd;
		Buttons m_buttons;
		std::vector<uint8_t> m_memory;
		std::list<uint32_t> m_lastPCs;
		bool m_dspResetRequest = false;
		bool m_dspResetCompleted = false;
		uint8_t m_dspInjectNmiRequest = 0;
	};
}
