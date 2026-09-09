#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>

namespace hwLib
{
	// SC-8850 I/O gate array. A0-A6 select the register; higher bits alias.
	// The board supplies register semantics and interrupt-source arbitration.
	class Tc160g22af
	{
	public:
		static constexpr uint16_t RegisterCount = 0x80;
		static constexpr uint8_t IrqSourceKey = 0;
		static constexpr uint8_t IrqSourceEncoder = 1;
		static constexpr uint8_t IrqSourceExternalBase = 2;
		static constexpr uint8_t ExternalIrqCount = 5;
		static constexpr uint8_t IrqSourceInternalBase = IrqSourceExternalBase + ExternalIrqCount;
		static constexpr uint8_t IrqSourceCount = 16;
		static constexpr uint8_t RegisterLcdCommand = 0x38;
		static constexpr uint8_t RegisterLcdData = 0x39;
		static constexpr uint8_t RegisterIrqSource = 0x40;
		static constexpr uint8_t RegisterEncoderDelta = 0x42;
		static constexpr uint8_t RegisterKeyData = 0x43;

		using IrqCallback = std::function<void(bool)>;

		void reset() { m_registers.fill(0); setIrqLevel(false); }

		static constexpr uint8_t registerIndex(const uint16_t _offset)
		{
			return static_cast<uint8_t>(_offset & (RegisterCount - 1));
		}

		static constexpr uint8_t externalIrqSource(const uint8_t _input)
		{
			return static_cast<uint8_t>(IrqSourceExternalBase + _input);
		}

		uint8_t read(uint16_t _offset) const { return m_registers[registerIndex(_offset)]; }
		void write(uint16_t _offset, uint8_t _value) { m_registers[registerIndex(_offset)] = _value; }

		void setIrqCallback(IrqCallback _callback) { m_irqCallback = std::move(_callback); }
		void setIrqLevel(const bool _level)
		{
			if(m_irqLevel == _level) return;
			m_irqLevel = _level;
			if(m_irqCallback) m_irqCallback(_level);
		}
		bool irqLevel() const { return m_irqLevel; }

	private:
		std::array<uint8_t, RegisterCount> m_registers{};
		IrqCallback m_irqCallback;
		bool m_irqLevel = false;
	};
}
