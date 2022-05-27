#pragma once

#include <array>
#include <vector>

#include "Moira.h"

#include "gpt.h"
#include "qsm.h"
#include "sim.h"

namespace mc68k
{
	class Mc68k : public moira::Moira
	{
	public:
		Mc68k();
		~Mc68k() override;

		virtual void exec();

		void injectInterrupt(uint8_t _vector, uint8_t _level);

		static void writeW(std::vector<uint8_t>& _buf, size_t _offset, uint16_t _value);
		static uint16_t readW(const std::vector<uint8_t>& _buf, size_t _offset);

		moira::u8 read8(moira::u32 _addr) override;
		moira::u16 read16(moira::u32 _addr) override;
		void write8(moira::u32 _addr, moira::u8 _val) override;
		void write16(moira::u32 _addr, moira::u16 _val) override;

	private:
		moira::u16 readIrqUserVector(moira::u8 level) const override;
		void raiseIPL();

		Gpt m_gpt;
		Sim m_sim;
		Qsm m_qsm;

		std::array<std::deque<uint8_t>, 8> m_pendingInterrupts;
	};
}
