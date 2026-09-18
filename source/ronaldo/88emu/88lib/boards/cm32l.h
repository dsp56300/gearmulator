#pragma once

#include "88lib/rom/rom.h"

#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "cpu/mcs96/machine.hpp"
#include "custom_chips/mt32reverb/mt32reverb.h"
#include "hardwareLib/sed1200.h"
#include "synthLib/midiBufferParser.h"
#include "synthLib/midiRateLimiter.h"

namespace emu88Lib
{
	// The CM-32L: Roland's LA board in its "computer music" trim. One 8095 drives the LA32
	// partial synthesizer, the Boss reverb gate array and - through a write-only latch - a
	// SED1200 display the case has no window for. The firmware writes to it all the same,
	// and looks at the (equally absent) buttons at power-on for its test and version screens.
	//
	// Memory map:
	//   0000-0FFF  bank and control latches, reverb and LCD registers, switches, LA32
	//   1000-7FFF  control ROM                              (bus ROM)
	//   8000-BFFF  bank window: control ROM, work RAM
	//   C000-FFFF  work RAM, the same 16 KiB as bank 10h    (bus RAM)
	//
	// The older MT-32 board differs in two places that are called out at their sites: it maps
	// the LA32 word-wide rather than a byte at a time, and its reverb parameters sit one bit
	// higher in the same three registers.
	class Cm32l
	{
	public:
		using SampleFrame = std::pair<int32_t, int32_t>;
		static constexpr uint32_t SampleRate = 32000;
		static constexpr uint32_t CpuClock = 12000000;
		// The NMOS 8095/8x9x takes three oscillator clocks per architectural state.
		static constexpr uint32_t CpuStateRate = CpuClock / 3;
		static constexpr uint32_t La32Clock = 16384000;
		static constexpr uint32_t La32SlotRate = La32Clock / 16;
		// The two groups of eight service switches. The case exposes none of them, but holding
		// one at power-on still reaches the board's hidden test and version screens.
		static constexpr uint32_t ButtonCount = 16;

		explicit Cm32l(const Cm32lRomSet& roms);
		bool isValid() const { return m_valid; }
		// Front-bezel lamps, bit 0 = MIDI MESSAGE. POWER is wired straight to the supply.
		uint8_t leds() const;
		const hwLib::Sed1200& lcd() const { return m_lcd; }
		void reset();
		SampleFrame renderSample();
		void setButtons(uint32_t buttons);
		// One byte of the 8095's on-chip register file, where the firmware keeps its working
		// state. Only the RAM above the SFRs is reachable, so reading has no side effects.
		// For tests and debugging; the board itself never looks at it.
		uint8_t registerByte(const uint8_t address)
		{
			return address >= mcs96::kRegisterRamBase
				? m_machine.cpu().register_ram()[address - mcs96::kRegisterRamBase] : 0;
		}
		void addMidiEvent(const synthLib::SMidiEvent& event, uint8_t port = 0);
		void readMidiOut(std::vector<synthLib::SMidiEvent>& events);
		void transportDiscontinuity(uint32_t generation);

	private:
		static constexpr uint32_t DirectRamBase = 0xc000, DirectRamSize = 0x4000;

		// Everything on the bus that is not plain ROM or RAM.
		struct Host final : emu::DeviceLE
		{
			explicit Host(Cm32l& board) : board(board) {}
			uint8_t read8(uint32_t address) override { return board.read(static_cast<uint16_t>(address)); }
			void write8(uint32_t address, uint8_t value) override { board.write(static_cast<uint16_t>(address), value); }
			Cm32l& board;
		};

		uint8_t read(uint16_t address);
		void write(uint16_t address, uint8_t value);
		uint8_t readBank(uint16_t offset) const;
		void writeBank(uint16_t offset, uint8_t value);
		void flushLcd(uint8_t control);
		void updateReverbParameters();
		float applyVca();
		uint8_t* directRam() { return m_machine.bus().ptr(DirectRamBase); }
		const uint8_t* directRam() const { return m_machine.bus().mem() + DirectRamBase; }

		// The control ROM is the only image the board itself keeps: the LA32 and the reverb
		// own theirs, so nothing holds a second copy of the 1 MiB PCM image.
		std::vector<uint8_t> m_control;
		mcs96::Machine m_machine{mcs96::Variant::I8x9x};
		Host m_host{*this};
		mt32ReverbLib::Mt32Reverb m_reverb;
		hwLib::Sed1200 m_lcd;
		std::vector<uint8_t> m_ramHigh;	// bank 11h
		std::vector<uint8_t> m_lcdBuffer;
		std::unique_ptr<synthLib::MidiRateLimiter> m_midiIn;
		synthLib::MidiBufferParser m_midiOut{synthLib::MidiEventSource::Device};
		std::array<uint8_t, 2> m_buttons{{0xff, 0xff}};
		uint8_t m_bank = 0;
		// The 0200h latch: the MIDI MESSAGE lamp in bit 0, the reverb program's
		// upper address lines in bits 1-2, the Boss clock in bit 5, the display's chip select
		// and clock in bits 6-7. The last three the gate array also acts on internally.
		uint8_t m_controlLatch = 0;
		uint8_t m_reverbTime = 0;
		uint8_t m_reverbLevel = 0;
		uint8_t m_port0 = 0;
		int64_t m_cpuRemainder = 0;
		// The VCA's control voltage, the PWM smoothed by R39/C44. Held as the gain it produces
		// rather than as volts, since the two are the same shape - see applyVca().
		float m_vcaGain = 0.0f;
		bool m_valid = false;
	};
}
