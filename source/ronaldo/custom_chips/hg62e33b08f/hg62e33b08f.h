#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace hg62e33b08fLib
{
	// Hitachi HG62E33B08F, the CPU gate array Roland put next to the 80C196KB
	// in the MV-30 and the S-760. It sits on the CPU's bus and owns three
	// things: the memory map, the interrupt line and a DMA channel.
	//
	// Memory map. The 64 KiB the CPU sees is four 16 KiB windows, each with
	// two bank registers, one for instruction fetches and one for data, so
	// code and its stack can sit at the same CPU address in different physical
	// banks. A bank value is a base in units of 1 KiB, so the physical address
	// is bank * 0x400 + (CPU address & 0x3FFF); what lives where is the
	// board's decode. Power-on maps every window to bank 0x780, where both
	// boards decode the boot ROM.
	//
	// Interrupts. Eight level inputs, numbered as the bits of the mask register,
	// share the one INT line, which the boards wire to EXTINT1. When the CPU
	// fetches the EXTINT1 vector (0x203A) the gate array puts the number of the
	// source it is serving in bits 2-4 of the word, so the vector lands on one
	// of a table of 4-byte entry stubs; that fetch acknowledges the source until
	// its input drops. The lowest-numbered pending source is served first.
	// Source 4 is the gate array's own DMA completion.
	//
	// DMA. A byte count, a 24-bit address and a control byte whose write arms
	// the channel. The board moves the bytes (it knows which device asked and
	// where the address goes) and steps the channel once per byte; the count
	// running out ends the transfer and raises source 4. Reading the control
	// byte acknowledges that.
	//
	// Registers, relative to the CPU's 0x0100 (byte addressed):
	//   0x00-0x07  instruction bank, windows 0-3 (one 16-bit word each)
	//   0x08-0x0F  data bank, windows 0-3
	//   0x10, 0x12 serial port / bus mode words, retained, not decoded
	//   0x14       serial port status: bit 1 reads set (transmitter ready)
	//   0x15       interrupt mask, bit n enables source n
	//   0x16       serial port data, retained
	//   0x18       DMA byte count (counts down)
	//   0x1A       DMA address bits 0-15 (counts up)
	//   0x1C       DMA address bits 16-23
	//   0x1D       DMA control: written last, arms the channel; bit 1 set =
	//              device to memory. Reading it acknowledges completion.
	//
	// The serial port is on the chip but the MV-30 does not wire it, and it is
	// not modelled beyond the transmitter-ready bit.
	class HG62E33B08F
	{
	public:
		static constexpr uint16_t RegisterBase = 0x0100;
		static constexpr uint16_t RegisterCount = 0x20;
		static constexpr uint16_t Extint1Vector = 0x203a;
		static constexpr uint16_t BootBank = 0x780;
		static constexpr uint32_t BankUnit = 0x400;
		static constexpr uint32_t WindowSize = 0x4000;
		static constexpr unsigned SourceCount = 8;
		static constexpr unsigned DmaSource = 4;

		// An instruction window (0-3) got a new bank.
		using BankCallback = std::function<void(unsigned _window, uint16_t _bank)>;
		using LineCallback = std::function<void(bool)>;

		HG62E33B08F();

		void reset();

		void setBankCallback(BankCallback _callback) { m_bankCallback = std::move(_callback); }
		void setIrqCallback(LineCallback _callback) { m_irqCallback = std::move(_callback); }

		// ---- Memory map ----
		// Bank of window `_window`: 0-3 instruction, 4-7 data.
		uint16_t bank(const unsigned _window) const { return word(static_cast<uint8_t>((_window & 7) * 2)); }
		uint32_t physical(const uint16_t _address, const bool _fetch) const
		{
			return bank((_address >> 14) + (_fetch ? 0 : 4)) * BankUnit + (_address & (WindowSize - 1));
		}

		// ---- CPU access to the registers ----
		uint8_t read(uint8_t _reg);
		// Without side effects, for debuggers.
		uint8_t peek(uint8_t _reg) const;
		void write(uint8_t _reg, uint8_t _value);
		// The CPU's fetch of the EXTINT1 vector: the table word with the source
		// in bits 2-4. Acknowledges that source. Untouched while INT is low.
		uint16_t vector(uint16_t _tableWord);

		// ---- Interrupt sources ----
		void setSource(unsigned _source, bool _level);
		bool irq() const { return m_irq; }

		// ---- DMA ----
		bool dmaActive() const { return m_dmaActive; }
		bool dmaToMemory() const { return (m_dmaControl & 0x02) != 0; }
		uint32_t dmaAddress() const { return m_dmaAddress; }
		uint8_t dmaControl() const { return m_dmaControl; }
		// One byte moved. Returns true when it was the last one: the board
		// strobes terminal count to the device.
		bool dmaStep();
		// The transfer was ended from outside (the firmware's own TC line).
		void dmaStop() { m_dmaActive = false; }

	private:
		uint16_t word(const uint8_t _reg) const
		{
			return static_cast<uint16_t>(m_regs[_reg & 0x1e] | (m_regs[(_reg & 0x1e) + 1] << 8));
		}
		void setWord(uint8_t _reg, uint16_t _value);
		uint8_t pending() const;
		void updateIrq();
		void setLine(bool _level);

		std::array<uint8_t, RegisterCount> m_regs{};
		uint8_t m_sources = 0;
		uint8_t m_acknowledged = 0;
		bool m_irq = false;

		bool m_dmaActive = false;
		uint32_t m_dmaAddress = 0;
		uint16_t m_dmaCount = 0;
		uint8_t m_dmaControl = 0;

		BankCallback m_bankCallback;
		LineCallback m_irqCallback;
	};
}
