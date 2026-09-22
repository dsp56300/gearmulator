#pragma once

#include "88lib/deviceModel.h"
#include "88lib/rom/rom.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "cpu/mcs96/machine.hpp"
#include "custom_chips/la32/la32.h"
#include "custom_chips/mt32reverb/mt32reverb.h"
#include "hardwareLib/sed1200.h"
#include "synthLib/midiBufferParser.h"
#include "synthLib/midiRateLimiter.h"

namespace emu88Lib
{
	// The MT-32's ten front-panel switches, by matrix position. The CM-32L board reads the
	// same matrix at power-on for its test and version screens, with nothing wired to it.
	enum class Mt32Button : uint8_t
	{
		Part1 = 0,
		Part2 = 1,
		Part3 = 2,
		SoundGroup = 3,
		Volume = 4,
		Part4 = 8,
		Part5 = 9,
		Rhythm = 10,
		Sound = 11,
		MasterVolume = 12,
	};

	constexpr uint32_t mt32ButtonBit(const Mt32Button _button)
	{
		return uint32_t{1} << static_cast<uint8_t>(_button);
	}

	// Roland's LA board: one 8095 drives the LA32 partial synthesizer, the Boss reverb gate
	// array and - through a write-only latch - a SED1200 display, and reads a 2 x 8 switch
	// matrix and, on the MT-32, the VOLUME/VALUE potentiometer on its A/D converter. The
	// MT-32 has the display and the switches on its front; the CM-32L is the same board in
	// a case with no window for them, and its firmware drives the display all the same and
	// looks at the absent switches at power-on for its test and version screens.
	//
	// Memory map:
	//   0000-0FFF  bank and control latches, reverb and LCD registers, switches, LA32
	//   1000-7FFF  control ROM                              (bus ROM)
	//   8000-BFFF  bank window: control ROM, work RAM
	//   C000-FFFF  work RAM, the same 16 KiB as bank 10h    (bus RAM)
	//
	// Two generations of the board exist, and LaModel says which one is being run. The
	// old-type MT-32 board (1.x firmware) puts a C8095-90 on a 16-bit bus: it maps the LA32
	// word-wide, takes the reverb parameters one bit higher in the same three registers,
	// wires the DAC one bit up from the audio bus and has no VCA. The new-type MT-32 board
	// (2.x firmware), the CM-32L and the CM-32LN put a P8098 or an 80C198 on an 8-bit bus,
	// map the LA32 a byte at a time, rotate the LA32's output onto the audio bus, and
	// control the level with a PWM-driven VCA. Each difference is called out at its site.
	class LaBoard
	{
	public:
		using SampleFrame = std::pair<int32_t, int32_t>;
		static constexpr uint32_t SampleRate = 32000;
		static constexpr uint32_t CpuClock = 12000000;
		static constexpr uint32_t La32Clock = 16384000;
		static constexpr uint32_t La32SlotRate = La32Clock / 16;
		// The two groups of eight switches of the panel matrix.
		static constexpr uint32_t ButtonCount = 16;
		// The knob's full travel on the 10-bit A/D converter.
		static constexpr uint16_t KnobMaximum = 1023;

		explicit LaBoard(const LaRomSet& roms);
		bool isValid() const { return m_valid; }
		LaModel model() const { return m_model; }
		// The NMOS 8095/8098 takes three oscillator clocks per architectural state, the
		// CHMOS 80C198 of the CM-32LN two.
		uint32_t cpuStateRate() const { return m_cpuStateRate; }
		// Front-panel lamps, bit 0 = MIDI MESSAGE. POWER is wired straight to the supply.
		uint8_t leds() const;
		const hwLib::Sed1200& lcd() const { return m_lcd; }
		void reset();
		SampleFrame renderSample();
		// Post-DAC mix/VCA in DAC-count units. Preserve fractional analog levels:
		// the hardware output path must not quantise this back to 16 bits.
		const std::pair<float, float>& analogSample() const { return m_analogSample; }
		// The six words the DAC converted for the last frame, as its inputs see them - after
		// the board's wiring of the LA32 and the DAC, before the analog sum and VCA: SYN1
		// L/R, SYN2 L/R, REV L/R. The digital regression reference, independent of the
		// calibration of anything analog.
		const std::array<int32_t, 6>& dacBuses() const { return m_dac; }
		void setButtons(uint32_t buttons);
		// The VOLUME/VALUE knob, 0 to KnobMaximum. The CM-32L boards leave the pin tied high.
		void setKnob(uint16_t position);
		void turnKnob(int32_t steps) { setKnob(static_cast<uint16_t>(std::clamp<int32_t>(m_knob + steps, 0, KnobMaximum))); }
		uint16_t knob() const { return m_knob; }
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

		// What the two board generations do to a 16-bit audio word on its way from the LA32
		// to the reverb and the DAC; public so the tests can pin them down.
		//
		// The new-type boards wire the LA32's serial output onto the audio bus rotated:
		// O15 stays the sign, O13-O0 move up one and O14 lands on SD0. Both the reverb's
		// input and the DAC take the bus, so a sound is twice as loud as the chip computed
		// it and folds back past +/-16383 - the overdrive the MT-32 is known for.
		static int32_t rotateNewBoardBus(int32_t word);
		// The old-type board keeps the bus straight, which is what its reverb reads, and
		// wires the DAC's inputs one bit up instead, BIT16 grounded: bit 14 is dropped and
		// the wet return folds along with the dry channels.
		static int32_t shiftOldBoardDac(int32_t word);

	private:
		static constexpr uint32_t DirectRamBase = 0xc000, DirectRamSize = 0x4000;

		// Everything on the bus that is not plain ROM or RAM.
		struct Host final : emu::DeviceLE
		{
			explicit Host(LaBoard& board) : board(board) {}
			uint8_t read8(uint32_t address) override { return board.read(static_cast<uint16_t>(address)); }
			void write8(uint32_t address, uint8_t value) override { board.write(static_cast<uint16_t>(address), value); }
			LaBoard& board;
		};

		bool isOldBoard() const { return m_model == LaModel::Mt32Old; }
		bool hasVca() const { return !isOldBoard(); }
		uint8_t read(uint16_t address);
		void write(uint16_t address, uint8_t value);
		uint8_t readBank(uint16_t offset) const;
		void writeBank(uint16_t offset, uint8_t value);
		void flushLcd(uint8_t control);
		void updateReverbParameters();
		float applyVca();
		uint8_t* directRam() { return m_machine.bus().ptr(DirectRamBase); }
		const uint8_t* directRam() const { return m_machine.bus().mem() + DirectRamBase; }

		const LaModel m_model;
		const uint32_t m_cpuStateRate;
		// The control ROM is the only image the board itself keeps: the LA32 and the reverb
		// own theirs, so nothing holds a second copy of the PCM image.
		std::vector<uint8_t> m_control;
		mcs96::Machine m_machine;
		Host m_host{*this};
		la32Lib::LA32 m_la32;
		mt32ReverbLib::Mt32Reverb m_reverb;
		hwLib::Sed1200 m_lcd;
		std::vector<uint8_t> m_ramHigh;	// bank 11h
		std::vector<uint8_t> m_lcdBuffer;
		std::unique_ptr<synthLib::MidiRateLimiter> m_midiIn;
		synthLib::MidiBufferParser m_midiOut{synthLib::MidiEventSource::Device};
		std::array<uint8_t, 2> m_buttons{{0xff, 0xff}};
		std::array<int32_t, 6> m_dac{};
		uint8_t m_bank = 0;
		// The 0200h latch: the MIDI MESSAGE lamp in bit 0, the reverb program's
		// upper address lines in bits 1-2, the Boss clock in bit 5, the display's chip select
		// and clock in bits 6-7. The last three the gate array also acts on internally.
		uint8_t m_controlLatch = 0;
		uint8_t m_reverbTime = 0;
		uint8_t m_reverbLevel = 0;
		uint8_t m_port0 = 0;
		uint16_t m_knob = KnobMaximum;
		int64_t m_cpuRemainder = 0;
		// R39/C44-smoothed control and the offset-corrected M5207L01 gain.
		float m_vcaControl = 0.0f;
		float m_vcaGain = 0.0f;
		std::pair<float, float> m_analogSample{};
		bool m_valid = false;
	};
}
