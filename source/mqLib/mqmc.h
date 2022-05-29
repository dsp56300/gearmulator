#pragma once

#include <array>
#include <list>

#include "../68kEmu/mc68k.h"

namespace mqLib
{
	class ROM;

	class MqMc : public mc68k::Mc68k
	{
	public:
		explicit MqMc(ROM& _rom);
		~MqMc();

		void exec();

	private:
		uint16_t read16(uint32_t addr) override;
		uint8_t read8(uint32_t addr) override;
		void write16(uint32_t addr, uint16_t val) override;
		void write8(uint32_t addr, uint8_t val) override;

		void dumpMemory(const char* _filename) const;
		void dumpAssembly(uint32_t _first, uint32_t _count) const;

		void onReset() override;

		ROM& m_rom;
		std::vector<uint8_t> m_memory;
		std::vector<uint8_t> m_sim;
		std::list<uint32_t> m_lastPCs;
	};
}
