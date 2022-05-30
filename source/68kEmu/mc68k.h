#pragma once

#include <array>
#include <vector>

#include "gpt.h"
#include "hdi08.h"
#include "qsm.h"
#include "sim.h"

namespace mc68k
{
	class Mc68k
	{
	public:
		Mc68k();
		~Mc68k();

		virtual void exec();

		void injectInterrupt(uint8_t _vector, uint8_t _level);

		virtual void onReset() {}

		static void writeW(std::vector<uint8_t>& _buf, size_t _offset, uint16_t _value);
		static uint16_t readW(const std::vector<uint8_t>& _buf, size_t _offset);

		virtual uint8_t read8(uint32_t _addr);
		virtual uint16_t read16(uint32_t _addr);
		virtual uint32_t read32(uint32_t _addr);
		virtual void write8(uint32_t _addr, uint8_t _val);
		virtual void write16(uint32_t _addr, uint16_t _val);
		virtual void write32(uint32_t _addr, uint32_t _val);

		uint32_t readIrqUserVector(uint8_t _level);

		void reset();
		void setPC(uint32_t _pc);
		uint32_t getPC();
		virtual uint32_t getResetPC() { return 0; }
		virtual uint32_t getResetSP() { return 0; }

		uint32_t disassemble(uint32_t _pc, char* _buffer) const;

		uint64_t getCycles() const { return m_cycles; }

		Hdi08& hdi08();

		Port& getPortE()	{ return m_sim.getPortE(); }
		Port& getPortF()	{ return m_sim.getPortF(); }
		Port& getPortGP()	{ return m_gpt.getPortGP(); }

	private:
		void raiseIPL();

		Gpt m_gpt;
		Sim m_sim;
		Qsm m_qsm;
		Hdi08 m_hdi08;

		std::array<std::deque<uint8_t>, 8> m_pendingInterrupts;
		uint64_t m_cycles = 0;
	};
}
