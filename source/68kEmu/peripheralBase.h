#pragma once

#include <cstdint>
#include <vector>

#include "peripheralTypes.h"

namespace mc68k
{
	class PeripheralBase
	{
	public:
		explicit PeripheralBase(uint32_t _base, uint32_t _size);

		bool isInRange(PeriphAddress _addr) const
		{
			const auto a = static_cast<uint32_t>(_addr);
			return a >= m_baseAddress && a < (m_baseAddress + m_buffer.size());
		}
		virtual uint8_t read8(PeriphAddress _addr);
		virtual uint16_t read16(PeriphAddress _addr);
		virtual void write8(PeriphAddress _addr, uint8_t _val);
		virtual void write16(PeriphAddress _addr, uint16_t _val);

	private:
		const uint32_t m_baseAddress;
		std::vector<uint8_t> m_buffer;
	};
}
