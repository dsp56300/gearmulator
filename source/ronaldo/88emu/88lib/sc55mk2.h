#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "sc55_submcu.h"
#include "rom.h"
#include "sc88types.h"

#include "custom_chips/gp/gp.h"
#include "cpu/h8500/machine.hpp"
#include "hardwareLib/hd44780.h"

namespace emu88Lib
{
	// =====================================================================
	// Sc55Mk2 — the SC-55mk2 board
	// =====================================================================
	//
	// Memory map (the H8/532's page nibble; the CPU only has 20 address
	// lines, so page 0x10 aliases page 0):
	//
	//   page 0   0x0000-0x7FFF   H8/532 on-chip ROM (32 KiB boot firmware)
	//            0x8000-0xDFFF   SRAM
	//            0xE000-0xE3FF   GP register window (address & 0x3F)
	//            0xE402          gate-array interrupt status; reading it
	//                            reports the line and drops IRQ1
	//            0xEC00-0xEFFF   sub-MCU window (address & 0xFF)
	//            0xFB80-0xFF7F   H8/532 on-chip RAM (the CPU handles this)
	//            0xFF80-0xFFFF   H8/532 SFRs (likewise; 0xFF80-0xFF8F and
	//                            0xFFFE-0xFFFF arrive as portRead/portWrite)
	//   pages 1-4, 8-9, 14-15    program ROM. The board scrambles the address:
	//                            bits 17-0 pass through, and address bit 19
	//                            supplies ROM bit 18 (see romAddress).
	//   pages 10-11              SRAM
	//
	class Sc55Mk2
	{
	public:

		// External interrupt pins as this board wires them: the GP's voice-end
		// line goes straight to IRQ0, the gate array multiplexes its own
		// sources onto IRQ1.
		enum IrqLine : int { IrqGp = 0, IrqGateArray = 1 };

		// Port 9's registers, which the H8/532 keeps outside its port block.
		// The board identification straps are read through them.
		static constexpr uint32_t AddrP9Ddr = 0xFFFE;
		static constexpr uint32_t AddrP9Dr  = 0xFFFF;

		using SampleFrame = std::pair<int32_t, int32_t>;	// left, right
		using Lcd = hwLib::Hd44780;
		using GP  = gpLib::GP;

		static constexpr uint32_t InternalRomSize = 0x8000;	// 32 KiB, on-chip
		static constexpr uint32_t ProgramRomSize  = 0x100000;	// up to 1 MiB
		static constexpr uint32_t SramSize        = 0x8000;	// 32 KiB battery RAM

		// Audio rate at the GP's pins.
		//
		// This is NOT a round number and it is not a free choice: the GP is
		// clocked at 24 MHz and spends 25 of those per voice time slot, so
		// with 28 voices one iteration takes (28+1)*25 = 725 clocks, i.e.
		// 33103.4 iterations/s — and the firmware turns on 2x oversampling
		// (config A bit 6), so the DAC emits two samples per iteration.
		// 24e6 / 725 * 2 = 66206.9 Hz. See GP::dacRate(), which computes
		// exactly this from the same two numbers.
		static constexpr uint32_t GpClockHz = 24000000;
		static constexpr uint32_t SampleRate = 66207;

		// H8/532 phi. The part is a 12 MHz grade and that is what the board
		// runs it at. The CPU core charges every instruction its Appendix-A.4
		// states, so this one rate paces the instruction stream and every
		// on-chip peripheral alike.
		static constexpr uint64_t CpuClockHz = 12000000;

		// Undo the board's PCM address/data line scramble, in place.
		//
		// The address permutation acts on the low 20 bits only, so it repeats
		// per 1 MiB chip; the data permutation is a fixed bit shuffle. Both
		// tables match the reference model's `unscramble` (its non-SC-88
		// variant — the SC-88 family scrambles differently, which is why that
		// one has its own table).
		//
		// Without this the GP reads structurally valid but wrong bytes: the
		// DPCM exponents come out scrambled, the accumulator integrates
		// nonsense and rails, and notes are audible but aperiodic.
		static void descrambleWaveRom(std::vector<uint8_t>& _rom);

		explicit Sc55Mk2(Sc55RomSet _roms);
		~Sc55Mk2() = default;

		Sc55Mk2(const Sc55Mk2&) = delete;
		Sc55Mk2& operator=(const Sc55Mk2&) = delete;

		bool isValid() const { return m_valid; }

		// ---- Audio ----
		// Advance the board by one audio frame and return the stereo output.
		// The H8 runs a fixed cycle budget per frame (Bresenham, so the CPU
		// rate does not drift against the sample clock) and the GP renders
		// once.
		SampleFrame renderSample();

		// ---- MIDI ----
		// Raw wire bytes into the sub-MCU's serial inputs. The model paces
		// them at 31250 baud, so a burst queues rather than arriving at once —
		// which is what makes its ring-buffer overruns happen where they
		// really would.
		enum MidiPort : uint8_t { MidiInA = 0, MidiInB = 1, ComputerPort = 2 };
		void sendMidiByte (uint8_t _byte, MidiPort _port = MidiInA);
		void sendMidiBytes(const uint8_t* _data, size_t _size, MidiPort _port = MidiInA);

		// Bytes the sub-MCU has driven out of MIDI OUT since the last call.
		void readMidiOut(std::vector<uint8_t>& _out);

		// ---- Front panel ----
		// Active-high bitmap; bit index = column * 8 + row, matching the Button
		// enum in sc88types.h. The main MCU drives the matrix through the
		// sub-MCU's P0 (columns, active low) and P1 (rows, active low).
		void     setButton(Button _button, bool _pressed);
		void     setButton(uint32_t _index, bool _pressed);
		void     setButtons(const uint32_t _bitmap) { m_buttons = _bitmap; }
		uint32_t buttons() const { return m_buttons; }

		// The two panel lamps, bit 0 = ALL and bit 1 = MUTE, in the order the
		// editor binds them. They share P0 with the matrix column select, see
		// panelWrite.
		uint8_t  leds() const { return m_leds; }

		Lcd&       lcd()       { return m_lcd; }
		const Lcd& lcd() const { return m_lcd; }

		// The sub-MCU's own time base. Its timing constants are expressed in
		// main-MCU cycles at this rate, and the board derives them from the
		// sample counter rather than from the H8's cycle counter, so MIDI
		// pacing and the Active Sensing interval do not drift with the CPU
		// timing model. The audio rate is not a round number, so the
		// conversion is done in 64-bit from the sample counter rather than
		// through a rounded per-sample constant.
		static uint64_t subMcuCycles(const uint64_t _samples)
		{
			return _samples * Sc55SubMcu::kMcuHz / SampleRate;
		}

		// Battery voltage as the A/D reads it. The firmware compares against a
		// threshold and shows "Battery Low" below it, so this is not cosmetic.
		static constexpr uint16_t AnalogBattery = 0x2a0;

		// Rear-panel mode switch, read through the A/D's channel 7 when the
		// gate array points the multiplexer at it. This is NOT cosmetic: the
		// firmware hands the position to the sub-MCU as its routing config, and
		// only position 3 (MIDI) routes MIDI IN to the sound engine — the other
		// three are COMPUTER-port modes that soft-thru it to the serial port
		// instead. Getting it wrong looks exactly like broken MIDI input.
		enum SwitchPosition : uint8_t
		{
			SwitchPc1  = 0,
			SwitchPc2  = 1,
			SwitchMac  = 2,
			SwitchMidi = 3,
		};
		void    setSwitchPosition(const uint8_t _p) { m_switchPosition = _p & 3; }
		uint8_t switchPosition() const { return m_switchPosition; }

		// The gate array's mode register (0xE401). Bit 0 blanks the LCD; bits
		// 3-2 are the A/D multiplexer select.
		uint8_t ioSd() const { return m_ioSd; }
		bool    lcdEnabled() const { return m_lcdEnabled; }

		// Whether the board is driving one of the two external interrupt
		// pins. Whether the CPU then takes it is a separate question — the
		// H8/532 gates IRQ0 / IRQ1 in P1CR.
		bool isIrqPending(const IrqLine _line) const { return m_irqLine[_line & 1]; }

		uint64_t cycles() const { return m_machine.now(); }

	protected:
		// ---- Board hooks, called from the chip model ----
		uint8_t  extRead8 (uint32_t _addr);
		void     extWrite8(uint32_t _addr, uint8_t _val);
		// A port data-register read, with the value the port model computed.
		uint8_t  portRead (unsigned _port, uint8_t _value);
		uint16_t analogRead(uint8_t _channel);
		void     requestIrq(int _line, bool _level)
		{
			if(_line < 0 || _line > 1)
				return;
			m_irqLine[_line] = _level;
			m_machine.intc().set_irq_pin(static_cast<unsigned>(_line), _level);
		}

		// The board's program-ROM address scramble: the low 18 address bits
		// pass through and address bit 19 supplies ROM bit 18, so the four
		// 256 KiB quarters of the ROM are not laid out in page order.
		static uint32_t romAddress(uint32_t _addr);

		uint8_t  gateArrayRead (uint16_t _addr);
		void     gateArrayWrite(uint16_t _addr, uint8_t _val);

		// Raise gate-array line _line. The CPU sees it as IRQ1 and reads the
		// status register at 0xE402 to find out which line it was.
		void     setGateArrayInt(uint8_t _line, bool _level);

		// Panel matrix, driven through the sub-MCU's pass-through ports.
		uint8_t  panelRead();
		void     panelWrite(uint8_t _columns);

	private:
		uint8_t  extRead8Raw(uint32_t _addr);
		// Set up the bus map and the chip's host hooks. Runs once, from the
		// constructor.
		void     wireChip();
		// One audio frame's CPU budget, stepped instruction by instruction so

		// ---- Chip ----
		//
		// H8/532 in expanded maximum mode 3: 20 address lines, so page H'10
		// aliases page 0. The on-chip mask ROM and the program ROM are mapped
		// into the bus image so the core fetches and caches from them;
		// everything else reaches the board's page decode.
		struct BoardBus final : h8500::Device
		{
			explicit BoardBus(Sc55Mk2& _b) : board(_b) {}
			uint8_t read8(uint32_t _a) override { return board.extRead8(_a); }
			void write8(uint32_t _a, uint8_t _v) override { board.extWrite8(_a, _v); }
			Sc55Mk2& board;
		};

		h8500::Machine m_machine {h8500::ChipModel::H8_532, 3};
		BoardBus       m_boardBus {*this};

		Sc55RomSet   m_roms;
		bool     m_valid = false;

		std::vector<uint8_t> m_sram = std::vector<uint8_t>(SramSize, 0);

		GP  m_gp { gpLib::GpConfig{ GpClockHz, false } };
		Lcd m_lcd;
		Sc55SubMcu m_subMcu;
		uint32_t m_buttons = 0;		// active-high, see setButtons
		uint8_t  m_panelColumns = 0xFF;	// last value the sub-MCU's P0 was given
		uint8_t  m_leds = 0;			// ALL / MUTE, decoded from that same write
		std::vector<uint8_t> m_midiOut;

		// ---- CPU pacing ----
		uint64_t m_samplesRendered = 0;
		uint64_t m_cycleTarget = 0;
		uint32_t m_cycleFrac = 0;

		bool     m_irqLine[2] = {};	// levels this board drives on IRQ0 / IRQ1

		// ---- Gate array ----
		// Eight interrupt lines multiplexed onto IRQ1. The status register
		// reports the line NUMBER, so line 0 is unusable — zero is "nothing
		// pending". A line only latches while its enable bit is set.
		std::array<bool, 8> m_gaInt {};
		uint8_t m_gaIntTrigger = 0;

		uint8_t m_gaIrqEnable = 0;
		uint8_t m_ioSd = 0;			// gate-array mode register, 0xE401
		bool    m_lcdEnabled = true;
		uint8_t m_switchPosition = SwitchMidi;
	};
}
