#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <utility>
#include <vector>

#include "88lib/boards/sc880Lcd.h"
#include "88lib/boards/sc88types.h"
#include "88lib/mcu/sc88_submcu.h"

#include "cpu/h8500/machine.hpp"
#include "hardwareLib/hd44780.h"
#include "custom_chips/lsp/lsp.h"
#include "custom_chips/xp/xp.h"

#include "synthLib/midiTypes.h"

namespace emu88Lib
{
	// SC-88Pro front-panel matrix. The Pro keeps the SC-88 wiring for the
	// common controls and replaces EQ/INST MAP with its two map selectors.
	// Positions 15 and 23 are named DelayR/DelayL after the panel legend, but
	// the firmware ignores them: pressing either changes nothing. DELAY is
	// printed under KEY SHIFT because it *is* KEY SHIFT with SC-88 MAP held,
	// which is how the owner's manual describes it (p.13) and how the firmware
	// behaves - measured on the LCD, both alone and as the chord.
	// The bottom three pairs are mode-dependent; the firmware interprets them
	// as VIB, envelope/filter, or EFX controls according to USER INST/SELECT.
	enum class Sc88ProButton : uint8_t
	{
		Power       = 0, // Unpopulated matrix position; the Pro's POWER switch cuts the mains supply.
		Sc88Map     = 1,
		Sc55Map     = 2,
		InstL       = 3,
		InstR       = 4,
		InstMute    = 5,
		InstAll     = 6,
		Preview     = 7,

		MidiChL     = 8,
		MidiChR     = 9,
		ChorusL     = 10,
		ChorusR     = 11,
		PanL        = 12,
		PanR        = 13,
		PartR       = 14,
		DelayR      = 15,

		KeyShiftL   = 16,
		KeyShiftR   = 17,
		ReverbL     = 18,
		ReverbR     = 19,
		LevelL      = 20,
		LevelR      = 21,
		PartL       = 22,
		DelayL      = 23,

		UserInst    = 24,
		Select      = 25,
		VibRateL    = 26,
		VibRateR    = 27,
		VibDepthL   = 28,
		VibDepthR   = 29,
		VibDelayL   = 30,
		VibDelayR   = 31,

		Count       = 32,
	};

	// SK-88Pro front-panel matrix: the same sub-MCU scan, four columns of eight,
	// laid out anew. The firmware names every position in its switch test (the
	// table after the SC-88Pro's, 13:E6A0 in the CPU-order image), and pressing
	// each one alone does what that name says. SHIFT is the modifier the SC-88Pro
	// does not have: SHIFT with a parameter arrow opens the utility prompts (Init
	// All on PART left, Init GM on PART right, Init GS / CM-64 on the INSTRUMENT
	// arrows, Set Mode on KEY SHIFT, Copy/Clear/Exchange on LEVEL and PAN).
	// Positions 28 and 29 carry the SC-88Pro's "xx 15 xx"/"xx 23 xx" placeholder
	// names and do nothing.
	enum class Sk88ProButton : uint8_t
	{
		Edit        = 0,
		Util        = 1,
		Drum        = 2,
		Shift       = 3,
		All         = 4,
		Mute        = 5,
		Sc55Map     = 6,
		Sc88Map     = 7,

		PartR       = 8,
		LevelR      = 9,
		ReverbR     = 10,
		KeyShiftR   = 11,
		PartL       = 12,
		LevelL      = 13,
		ReverbL     = 14,
		KeyShiftL   = 15,

		InstR       = 16,
		PanR        = 17,
		ChorusR     = 18,
		MidiChR     = 19,
		InstL       = 20,
		PanL        = 21,
		ChorusL     = 22,
		MidiChL     = 23,

		OctR        = 24,
		OctL        = 25,
		Local       = 26,
		Function    = 27,
		Unused28    = 28,
		Unused29    = 29,
		UserInst    = 30,
		Select      = 31,

		Count       = 32,
	};

	// SC-880 front-panel matrix: the same 4x8 sub-MCU scan, its columns selected active
	// low. The names are the firmware's switch test table (11:CEB4 in the image), in
	// position order; every position not listed is "xx nn xx" there. The four cursor
	// keys carry LCD arrow glyphs in that table and were told apart by pressing them on
	// a paged screen. The dial is two switches, its quadrature phases, plus its push.
	enum class Sc880Button : uint8_t
	{
		Write       = 0,
		Enter       = 1,
		Info        = 2,
		Exit        = 3,
		DialInc     = 4,	// "ENC_INC"; pressed alone it steps the value up
		Preview     = 6,
		DialPush    = 7,	// "ENC"
		Tone        = 8,
		System      = 9,
		Rhythm      = 10,
		Utility     = 11,
		DialDec     = 12,	// "ENC_DEC"; pressed alone it steps the value down
		Perf        = 16,
		Mfx         = 17,
		Patch       = 18,
		Edit        = 19,
		Right       = 20,
		PartL       = 24,
		Left        = 25,
		Up          = 26,
		Down        = 27,
		PartR       = 28,

		Count       = 32,
	};

	// The SC-880's lamps as Sc88Pro::leds() reports them. Bits 0-7 are gate array C100
	// (active high), lit for the mode in force; WRITE and INFO are C101 bits 0 and 1
	// (active low), and INFO also flickers with incoming MIDI. Told apart by the test
	// mode's LED test (T-4), which steps through them in this order: PERFORM to RHYTHM,
	// WRITE, INFO, then M-FX to UTILITY.
	enum class Sc880Led : uint8_t
	{
		Utility = 0, System = 1, Edit = 2, Mfx = 3,
		Perform = 4, Patch = 5, Tone = 6, Rhythm = 7,
		Write = 8, Info = 9,
	};

	// =====================================================================
	// Sc88Pro — the SC-88 Pro / VE-GS Pro board
	// =====================================================================
	//
	// Same building blocks as the SC-88 (H8/510 + the MIDI sub-MCU + a gate
	// array + the XP tone generator) plus the Boss MB87837 "LSP" effects DSP,
	// but on a completely different bus, so this is NOT an Sc88 subclass:
	// Sc88 hardcodes the SC-88's paging, and the Pro's map shares none of it.
	//
	// Memory map, from the firmware itself:
	//
	//   pages 0x00-0x7F   ROM, 1 MiB mirrored (addr & 0xFFFFF); the SK-88Pro's
	//                     2 MiB image mirrored the same way. The page-0
	//                     SRAM overlay the SC-88 has is GONE, which is why the
	//                     reset vector can be 00:8D1A.
	//   pages 0xC0-0xC7   SRAM, 64 KiB mirrored; the SC-880's 256 KiB fills
	//                     C0-C3 and is mirrored once
	//   page  0xC8        XP tone generator
	//   pages 0xE0-0xE7   MIDI sub-MCU — the SC-88's register set, moved
	//   page  0xEF        gate array — the SC-88's registers, moved
	//   page  0xF0        LSP host interface
	//
	// Three control ROMs run on it: the SC-88Pro's own 1.02 ("SC-88ProVL
	// BOARD"), an "SC-GS" rev A '96 with the same panel code, and the headless
	// VE-GS Pro's "SC-GS" rev B '97, which the player offers as its own device.
	// The SK-88Pro keyboard runs on it too, from the 2 MiB image
	// buildSk88ProImage() makes, with its own panel matrix (Sk88ProButton); its
	// keyboard and controllers are not wired. So does the SC-880, from its own
	// 2 MiB control ROM ("GS-64 VER=3.00 SC-880"), with four times the SRAM, a
	// dot-matrix display on the gate array's LCD port (Sc880Lcd) and its own
	// panel matrix (Sc880Button).
	class Sc88Pro
	{
	public:
		// The product the board is built into. The SK-88Pro and the SC-880 both run
		// 2 MiB images, so the image size cannot tell them apart.
		enum class Variant : uint8_t { Sc88Pro, Sk88Pro, Sc880 };

		// External interrupt pins as this board wires them. The gate array
		// shares IRQ0 through its own status register; the XP is one line over.
		enum IrqLine : int { IrqGateArray = 0, IrqXp = 1, IrqSubMcu = 2 };

		using SampleFrame = std::pair<int32_t, int32_t>;
		using Lcd = hwLib::Hd44780;
		using Lsp = lspLib::LSPDispatcher;

		static constexpr uint32_t RomSize  = 0x100000;	// 1 MiB
		static constexpr uint32_t Sk88ProRomSize = 0x200000;
		static constexpr uint32_t Sc880RomSize = 0x200000;
		static constexpr uint32_t SramSize = 0x10000;
		// The SC-880 firmware addresses SRAM pages C0-C3: it stages the XP's
		// parameter blocks in C1 before copying them to the chip (01:5315).
		static constexpr uint32_t Sc880SramSize = 0x40000;
		static constexpr uint32_t WaveRomSize = 20 * 1024 * 1024;
		// P5DR is not an unconnected input on the SC-88Pro. The service schematic
		// hardwires P5.7..P5.0 to 0,0,1,0,1,1,0,1 respectively.
		static constexpr uint8_t P5Data = 0x2d;

		// Page numbers, not addresses — extRead8 dispatches on addr >> 16.
		enum Page : uint8_t
		{
			PageRomLast   = 0x7F,
			PageSramFirst = 0xC0,
			PageSramLast  = 0xC7,
			PageXp        = 0xC8,
			PageSubMcuFirst = 0xE0,
			PageSubMcuLast  = 0xE7,
			PageGateArray = 0xEF,
			PageLsp       = 0xF0,
		};

		// Sub-MCU registers, identical to the SC-88's apart from three new ones.
		enum SubMcuReg : uint16_t
		{
			SmWindowSize = 0x0100,
			SmSysExStage = 0x0014,
			SmVersionHi  = 0x00C0,
			SmVersionLo  = 0x00C1,
			SmStart      = 0x00C2,
			SmRouting0   = 0x00D0,
			SmNew4       = 0x00D4,	// not present on the SC-88
			SmNew5       = 0x00D5,
			SmNew7       = 0x00D7,
			SmCommand    = 0x00DC,
			SmChannel    = 0x00DD,
			SmParam1     = 0x00DE,
			SmParam2     = 0x00DF,
			SmSemaphore  = 0x00FD,
			SmPanelData  = 0x00FE,
			SmPanelCtrl  = 0x00FF,
		};

		enum GateArrayReg : uint16_t
		{
			GaLeds       = 0xC100,
			GaLedControl = 0xC101,
			GaIrqStatus  = 0xC104,
			GaIrqMask    = 0xC105,
			GaLcdStart   = 0xC11E,
			GaLcdInstr   = 0xC11F,
			GaLcdData    = 0xC120,
			GaLcdDataEnd = 0xC12C,
		};

		explicit Sc88Pro(std::vector<uint8_t> _firmware,
		                 const std::vector<uint8_t>& _waveRom = {},
		                 bool _factoryReset = true,
		                 Variant _variant = Variant::Sc88Pro);

		// The Pro's descriptor bank bytes run 00..43. Bits 4-6 select one of
		// five 4 MiB windows and bits 0-1 select a 1 MiB quarter, exposing the
		// complete 8 + 8 + 4 MiB VE-GS Pro ROM set. This differs from
		// the SC-88's 2 MiB-per-CS layout.
		~Sc88Pro() = default;

		bool isValid() const { return m_valid; }

		SampleFrame renderSample();

		// MIDI in. Which path is used is decided from the ROM's own vector
		// table: a board whose IRQ2 vector is the unused-vector stub has no
		// sub-MCU and takes MIDI on the H8's SCI instead (the VE-GS Pro), while
		// one with a real IRQ2 handler uses the mailbox (the SC-88 Pro).
		void addMidiEvent(const synthLib::SMidiEvent& _event, uint8_t _port = 0);

		void readMidiOut(std::vector<synthLib::SMidiEvent>& _events) { m_subMcu.readMidiOut(_events); }

		bool usesSerialMidi() const { return m_serialMidi; }
		size_t midiInBacklog() const { return m_midiInQueue.size() + (m_midiMailboxFull ? 1u : 0u); }

		// Bytes the firmware has transmitted, per SCI channel.
		const std::vector<uint8_t>& serialOut(uint8_t _ch) const { return m_serialOut[_ch & 1]; }

		// Matrix positions as the sub-MCU scans them: Sc88ProButton on the SC-88Pro, Sk88ProButton on
		// the SK-88Pro, Sc880Button on the SC-880. The Pro's position 0 is not populated; the SK's is
		// EDIT and the SC-880's WRITE.
		void setButtons(const uint32_t _b)
		{
			m_buttons = m_variant != Variant::Sc88Pro ? _b : _b & ~(uint32_t{1} << static_cast<uint8_t>(Sc88ProButton::Power));
		}
		uint32_t buttons() const { return m_buttons; }
		// The SC-880's VALUE dial. It is two contacts in the matrix, and the firmware takes
		// every closing of ENC_INC as a step up and of ENC_DEC as a step down, not a quadrature
		// pair: each detent is a pulse on one of them, long enough for the panel scan to see.
		// The scan and the page redraw take one closing per ~150 ms, too slow for a drag, so on
		// a verified ROM all but the last pending detent go straight into the firmware's key
		// queue and the last one closes the contact, whose own key event wakes the UI to take
		// them all. Unknown ROMs get a pulse per detent.
		void turnDial(int32_t _detents);
		// Bits 0-7 are LDD0-7 (bit 7 is green); bit 8 is the red User Inst LED.
		// The SK-88Pro's lamps are reported in the same order, as far as it has them.
		// The SC-880's are not mapped yet and read as dark.
		uint16_t leds() const;

		// The character module of every board but the SC-880, which has graphicLcd() instead.
		Lcd&       lcd()       { return m_lcd; }
		const Lcd& lcd() const { return m_lcd; }
		bool hasGraphicLcd() const { return m_variant == Variant::Sc880; }
		const Sc880Lcd& graphicLcd() const { return m_graphicLcd; }
		// P6DR bit 0, manipulated by the firmware's retained standby handler. The Pro's
		// physical POWER switch cuts the mains supply and cannot invoke that handler.
		bool lcdEnabled() const { return m_lcdEnabled; }

		uint64_t cycles() const { return m_machine.now(); }

	protected:
		// ---- Board hooks, called from the chip model ----
		uint8_t  extRead8 (uint32_t _addr);
		void     uartTransmit (uint8_t _byte);
		void     uart2Transmit(uint8_t _byte);
		void     extWrite8(uint32_t _addr, uint8_t _val);
		// A port data-register read, with the value the port model computed.
		uint8_t  portRead (uint32_t _addr, uint8_t _value);
		void     portWrite(uint32_t _addr, uint8_t _val);
		void     uartReceive (uint8_t _byte) { m_machine.sci(0).receive_byte(_byte); }
		void     uart2Receive(uint8_t _byte) { m_machine.sci(1).receive_byte(_byte); }
		void     requestIrq(int _line, bool _level)
		{
			if(_line >= 0)
				m_machine.intc().set_irq_pin(static_cast<unsigned>(_line), _level);
		}

	private:
		// Set up the bus map and the chip's host hooks. Runs once, from the
		// constructor.
		void    wireChip();
		// SRAM repeats every SRAM size across pages C0-C7.
		uint32_t sramOffset(const uint32_t _addr) const
		{
			return (_addr - (uint32_t{PageSramFirst} << 16)) & static_cast<uint32_t>(m_sram.size() - 1);
		}
		uint8_t subMcuRead (uint16_t _addr);
		void    subMcuWrite(uint16_t _addr, uint8_t _val);
		uint8_t gateArrayRead (uint16_t _addr);
		void    gateArrayWrite(uint16_t _addr, uint8_t _val);
		void    setGateArrayInt(uint8_t _line, bool _level);
		void    pumpMidiIn();
		void    runFactoryReset();
		void    powerCycle();

		// ---- Chip ----
		//
		// H8/510 in expanded maximum mode with a 16-bit external bus (mode 4).
		// The ROM image is mapped into the bus so the core fetches and caches
		// from it; everything else reaches the board's page decode.
		struct BoardBus final : h8500::Device
		{
			explicit BoardBus(Sc88Pro& _b) : board(_b) {}
			uint8_t read8(uint32_t _a) override { return board.extRead8(_a); }
			void write8(uint32_t _a, uint8_t _v) override { board.extWrite8(_a, _v); }
			Sc88Pro& board;
		};

		h8500::Machine m_machine {h8500::ChipModel::H8_510, 4};
		BoardBus       m_boardBus {*this};

		std::vector<uint8_t> m_rom;
		std::vector<uint8_t> m_sram;
		std::vector<uint8_t> m_waveRom;

		xpLib::XP m_xp;
		Lcd m_lcd;
		Sc880Lcd m_graphicLcd;
		bool m_lcdEnabled = true;
		bool m_valid = false;

		uint64_t m_samplesRendered = 0;
		uint64_t m_cycleTarget = 0;
		uint32_t m_cycleFrac = 0;

		// ---- front panel ----
		uint32_t m_buttons = 0;
		uint8_t  m_scanColumn = 0;
		// Where a firmware keeps its panel key queue, selected by the control ROM's hash.
		struct DialFirmware
		{
			uint16_t scannedRows;		// the matrix as last scanned, a byte per column
			uint16_t processedRows;		// the matrix as the edge detector last took it
			uint16_t keyReadPtr, keyWritePtr;
			uint16_t keyRingFirst, keyRingEnd;
			uint16_t keyState;			// a byte per key code; bit 1 = held
			uint8_t  pushPage;			// the routine that pushes a key code, which the
			uint16_t pushFirst, pushEnd;	// injection must not interrupt
		};
		static const DialFirmware* findDialFirmware(const std::vector<uint8_t>& _rom);
		void     stepDial();
		bool     dialKeyTaken(uint32_t _contact, bool _closed) const;
		bool     canQueueDialKeys() const;
		bool     queueDialDetent(uint8_t _code);
		const DialFirmware* m_dialFirmware = nullptr;
		int32_t  m_dialPending = 0;
		uint32_t m_dialContact = 0;		// the matrix bit the dial holds closed
		uint32_t m_dialReleased = 0;	// the matrix bit the dial last let go of
		uint32_t m_dialCountdown = 0;	// samples left in the current pulse or gap

		// ---- gate array ----
		std::array<bool, 4> m_gaInt{};
		uint8_t  m_gaIrqMask = 0x0f;	// 1 = masked
		uint8_t  m_gaIntTrigger = 0;
		uint8_t  m_leds = 0;
		uint8_t  m_ledControl = 0;
		uint8_t  m_lcdInstr = 0;
		uint8_t  m_lcdStaged = 0;
		std::array<uint8_t, GaLcdDataEnd - GaLcdData + 1> m_lcdBuffer{};
		emu::Scheduler::EventId m_gaLcdEvent = 0;	// pending LCD-ready pulse

		// ---- sub-MCU ----
		std::array<uint8_t, SmWindowSize> m_subMcuRam{};
		bool m_subMcuStarted = false;	// set when the CPU writes SmStart
		Sc88SubMcu m_subMcu;
		std::deque<Sc88SubMcu::Record> m_midiInQueue;
		Sc88SubMcu::Record m_midiMailbox;
		bool     m_midiMailboxFull = false;
		uint32_t m_midiWireDelay = 0;
		uint32_t m_midiWireFrac  = 0;

		// ---- LSP ----
		//
		// The chip owns its complete host aperture. The board only maps it into
		// CPU space and wires its serial audio path in renderSample().
		Lsp      m_lsp;
		bool     m_lspEnabled = true;
		bool     m_xpEnabled = true;
		uint8_t  m_p3dr = 0;
		bool     m_lspReturnEnabled = false;

		bool m_serialMidi = false;
		Variant m_variant;
		std::array<std::vector<uint8_t>, 2> m_serialOut;

	};
}
