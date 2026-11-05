#pragma once

#include "88lib/boards/laBoard.h"
#include "88lib/rom/rom.h"

#include <array>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "cpu/m6801/hc11machine.hpp"
#include "cpu/m6801/mc6850.hpp"
#include "hardwareLib/hd44780.h"
#include "synthLib/midiBufferParser.h"

namespace emu88Lib
{
	// The PRO-E's switches the player offers, as bits of ProE::setButtons(): the ones a style is
	// picked and played with. The panel has 88; the rest edit the arranger's memory and parts.
	enum class ProEButton : uint8_t
	{
		StartStop,
		IntroEnd,
		FillOriginal,
		FillVariation,
		SyncStart,
		SyncStop,
		ArrangerHold,
		AdvancedArranger,
		Variation,
		TapTempo,
		FadeIn,
		FadeOut,
		Style,
		GroupAB,
		Bank1, Bank2, Bank3, Bank4, Bank5, Bank6, Bank7, Bank8,
		Number1, Number2, Number3, Number4, Number5, Number6, Number7, Number8,

		Count
	};
	static_assert(static_cast<unsigned>(ProEButton::Count) <= 32);

	// The PRO-E's lamps the player shows, as bits of ProE::leds(): those of the arranger switches
	// that have one, and the four beat lamps. The LED matrix follows the key matrix: LED row r + 1,
	// bit b is the lamp of the switch at key row r, bit b. BANK, NUMBER and the fills have none.
	enum class ProELed : uint8_t
	{
		StartStop,
		IntroEnd,
		SyncStart,
		SyncStop,
		ArrangerHold,
		AdvancedArranger,
		Variation,
		FadeIn,
		FadeOut,
		Style,
		GroupAB,
		// Row 6 bits 3-0 count the beats of the bar while a style plays; stopped, beat 1 blinks at the tempo.
		Beat1, Beat2, Beat3, Beat4,

		Count
	};
	static_assert(static_cast<unsigned>(ProELed::Count) <= 16);

	// =====================================================================
	// ProE - the Roland PRO-E Intelligent Arranger
	// =====================================================================
	//
	// The main board: an MC68HC11A0 in its expanded mode at E = 2 MHz (the 16 MHz crystal halved
	// into EXTAL), an MC6850 for MIDI IN / OUT, a 2 x 16 character LCD, the panel's key matrix and
	// LEDs, the TEMPO / VALUE dial, an MB63H149 key assigner and banked memory:
	//
	//   0000-00FF  HC11 internal RAM          1000-103F  HC11 registers
	//   1200       key matrix: 8 switches of the row PA5-PA3 select; PA2-PA0 carry three more
	//   1400-1401  MC6850 (IRQ on XIRQ)
	//   160D       LCD control lines: bit 0 E, bit 1 R/W, bit 2 RS
	//   160E       LCD data bus
	//   180D       bank latch: bits 7-4 the 4000-BFFF ROM bank, bits 1-0 the 2000-3FFF RAM bank
	//   180E       LED latch, active low, lighting the row PA5-PA3 select (rows 1-6)
	//   1A00-1BFF  MB63H149 (its SRAM; the key assigner itself is not modelled)
	//   2000-3FFF  32 KiB battery-backed RAM, four banks
	//   4000-BFFF  pattern ROM banks 0-7, 8-15 the style card slot (empty here)
	//   C000-FFFF  program ROM
	//
	// The dial pulses PA7, which the pulse accumulator counts, with its direction on PD2.
	//
	// The firmware turns INTERNAL MEMORY protection on at every power-on: the code at C07A clears
	// the system area and loads 2451-2452 from a default word in the style ROM, bit 7 of 2452 being
	// the protection, and a SysEx bulk dump received with it on only shows INTERNAL MEMORY
	// PROTECTED. Here that power-on store leaves it off unless setMemoryProtectAtPowerOn() asks
	// otherwise; the PARAMETER menu still switches it as on the unit.
	//
	// The arranger plays through the CK-34 sound board, which hangs off the HC11's SCI. That link
	// is MIDI at 31250 baud addressed to an MT-32-class synthesizer: SysEx with model ID 16h for
	// the system and patch temporary areas, the parts on channels 2-9, rhythm on 10.
	//
	// Register map and key matrix after MAME's driver (roland_proe.cpp).
	class ProE
	{
	public:
		using SampleFrame = std::pair<int32_t, int32_t>;

		static constexpr uint32_t SampleRate = LaBoard::SampleRate;
		static constexpr uint32_t CpuClock = 8000000;	// EXTAL
		static constexpr uint32_t EClock = CpuClock / 4;
		static constexpr size_t RamSize = 0x8000;

		ProE(const ProERomSet& _roms, const LaRomSet& _soundBoard);

		ProE(const ProE&) = delete;
		ProE& operator=(const ProE&) = delete;

		bool isValid() const { return m_valid && m_sound.isValid(); }
		void reset();
		// Whether INTERNAL MEMORY protection comes up on, as the firmware has it, from the next power-on.
		void setMemoryProtectAtPowerOn(const bool _protect) { m_protectAtPowerOn = _protect; }

		// ---- Audio: the CM-32L's ----
		SampleFrame renderSample();
		const std::pair<float, float>& analogSample() const { return m_sound.analogSample(); }

		// ---- MIDI IN / OUT, through the ACIA ----
		void addMidiEvent(const synthLib::SMidiEvent& _event);
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _events);
		void transportDiscontinuity(uint32_t _generation);

		// ---- Front panel ----
		// The arranger switches, bit n = ProEButton n.
		void setButtons(uint32_t _buttons);
		// Any switch of the matrix: row 0-7 (PA5-PA3), bit 0-10.
		void setKey(unsigned _row, unsigned _bit, bool _pressed);
		// Turn the dial by _detents steps, positive clockwise; queued behind a turn in progress.
		void turnDial(int _detents);
		// The LEDs per row, a set bit lit: what the eye sees of the multiplexed matrix over the last
		// LedWindow E cycles.
		const std::array<uint8_t, 8>& ledRows() const { return m_leds; }
		// The lamps of ProELed, bit n = ProELed n.
		uint16_t leds() const;
		static constexpr uint64_t LedWindow = EClock * 3 / 100;
		const hwLib::Hd44780& lcd() const { return m_lcd; }

		// ---- Debugging ----
		m6801::Hc11Machine& machine() { return m_machine; }
		const LaBoard& soundBoard() const { return m_sound; }
		// The 32 KiB of banked RAM, bank 0 first.
		const std::vector<uint8_t>& ram() const { return m_ram; }
		uint64_t linkBytes() const { return m_linkBytes; }
		// Every byte sent on the link to the sound board, as it goes out.
		void setLinkMonitor(std::function<void(uint8_t)> _monitor) { m_linkMonitor = std::move(_monitor); }

	private:
		static constexpr uint32_t IoBase = 0x1080, IoEnd = 0x2000;
		static constexpr uint32_t RamWindow = 0x2000, RamWindowSize = 0x2000;
		static constexpr uint32_t RomWindow = 0x4000, RomWindowSize = 0x8000;
		static constexpr uint32_t ProgramBase = 0xc000;

		// 1080-1FFF: the board's I/O decoder.
		struct Io final : emu::Device
		{
			explicit Io(ProE& _board) : board(_board) {}
			uint8_t read8(uint32_t _addr) override { return board.ioRead(_addr); }
			void write8(uint32_t _addr, uint8_t _value) override { board.ioWrite(_addr, _value); }
			ProE& board;
		};
		// 2000-3FFF: the RAM bank the latch selects.
		struct RamWindowDevice final : emu::Device
		{
			explicit RamWindowDevice(ProE& _board) : board(_board) {}
			uint8_t read8(uint32_t _addr) override { return board.m_ram[board.ramOffset(_addr)]; }
			void write8(uint32_t _addr, uint8_t _value) override { board.ramWrite(_addr, _value); }
			ProE& board;
		};

		uint8_t ioRead(uint32_t _addr);
		void ioWrite(uint32_t _addr, uint8_t _value);
		size_t ramOffset(uint32_t _addr) const { return size_t(m_ramBank) * RamWindowSize + (_addr - RamWindow); }
		void ramWrite(uint32_t _addr, uint8_t _value);
		void selectRomBank(uint8_t _bank);
		void lcdLines(uint8_t _value);
		void ledIntegrate();
		uint8_t portAPins() const;
		static void onDial(void* _self, uint64_t _when, uint64_t _now);

		bool m_valid = false;
		std::vector<uint8_t> m_program;
		std::vector<uint8_t> m_patterns;

		m6801::Hc11Machine m_machine;
		m6801::Mc6850 m_acia{m_machine.sched(), m_machine.cpu()};
		hwLib::Hd44780 m_lcd{16, 2};
		Io m_io{*this};
		RamWindowDevice m_ramDevice{*this};

		std::vector<uint8_t> m_ram = std::vector<uint8_t>(RamSize, 0);
		std::array<uint8_t, 0x800> m_keyAssigner{};
		uint8_t m_romBank = 0xff, m_ramBank = 0;
		bool m_protectAtPowerOn = false;

		// LCD: the control-line latch, the bus as the CPU wrote it, the byte read while E was high
		uint8_t m_lcdLines = 0, m_lcdBus = 0, m_lcdRead = 0;

		std::array<uint16_t, 8> m_keys{};
		uint8_t m_keyRow = 0;
		// The LED matrix: the latch lights its row of PA5-PA3. The firmware refreshes a row every
		// 2.5 ms, and the main loop's key scan moves the rows in between, so an LED is taken as lit
		// when it was driven for a share of the window a scanned row gets, not from single writes.
		uint8_t m_ledLatch = 0;
		uint64_t m_ledSince = 0, m_ledWindowStart = 0;
		std::array<std::array<uint32_t, 8>, 8> m_ledTime{};
		std::array<uint8_t, 8> m_leds{};
		uint32_t m_buttons = 0;

		int m_dialPending = 0;
		bool m_dialPulseLow = false;
		bool m_pa7 = true;
		emu::Scheduler::EventId m_dialEvent = 0;

		// The CK-34 stand-in and the link to it
		LaBoard m_sound;
		synthLib::MidiBufferParser m_link{synthLib::MidiEventSource::Device};
		std::vector<synthLib::SMidiEvent> m_linkEvents;
		uint64_t m_linkBytes = 0;
		std::function<void(uint8_t)> m_linkMonitor;
		synthLib::MidiBufferParser m_midiOut{synthLib::MidiEventSource::Device};

		uint32_t m_cycleFraction = 0;
	};
}
