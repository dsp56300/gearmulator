#pragma once

#include <cassert>

#include "n2xtypes.h"

#include "mc68k/hdi08periph.h"

namespace n2x
{
	using Hdi08DspA = mc68k::Hdi08Periph<g_dspAAddress>;
	using Hdi08DspB = mc68k::Hdi08Periph<g_dspBAddress>;

	class Hdi08DspBoth : public mc68k::PeripheralBase<g_dspBothAddress, g_dspAAddress - g_dspBothAddress>
	{
	public:
		static constexpr uint32_t OffsetA = g_dspAAddress - base();
		static constexpr uint32_t OffsetB = g_dspBAddress - base();

		Hdi08DspBoth(Hdi08DspA& _a, Hdi08DspB& _b) : m_a(_a), m_b(_b)
		{
		}

		void write8(const mc68k::PeriphAddress _addr, const uint8_t _val) override
		{
			m_a.write8(offA(_addr), _val);
			m_b.write8(offB(_addr), _val);
		}

		void write16(const mc68k::PeriphAddress _addr, const uint16_t _val) override
		{
			m_a.write16(offA(_addr), _val);
			m_b.write16(offB(_addr), _val);
		}

		uint8_t read8(const mc68k::PeriphAddress _addr) override
		{
			assert(false && "not readable");
			return m_a.read8(_addr);
		}

		uint16_t read16(const mc68k::PeriphAddress _addr) override
		{
			assert(false && "not readable");
			return m_a.read16(_addr);
		}

		static mc68k::PeriphAddress offA(const mc68k::PeriphAddress _addr)
		{
			return off<OffsetA>(_addr);
		}

		static mc68k::PeriphAddress offB(const mc68k::PeriphAddress _addr)
		{
			return off<OffsetB>(_addr);
		}

		template<uint32_t Off> static mc68k::PeriphAddress off(mc68k::PeriphAddress _addr)
		{
			return static_cast<mc68k::PeriphAddress>(static_cast<uint32_t>(_addr) + Off);
		}
	private:
		Hdi08DspA& m_a;
		Hdi08DspB& m_b;
	};
}
