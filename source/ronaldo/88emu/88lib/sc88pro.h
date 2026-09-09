#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <utility>
#include <vector>

#include "sc88types.h"
#include "sc88_submcu.h"

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
		Power       = 0,
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
	//   pages 0x00-0x7F   ROM, 1 MiB mirrored (addr & 0xFFFFF). The page-0
	//                     SRAM overlay the SC-88 has is GONE, which is why the
	//                     reset vector can be 00:8D1A.
	//   pages 0xC0-0xC7   SRAM
	//   page  0xC8        XP tone generator
	//   pages 0xE0-0xE7   MIDI sub-MCU — the SC-88's register set, moved
	//   page  0xEF        gate array — the SC-88's registers, moved
	//   page  0xF0        LSP host interface
	//
	// The two ROMs this covers both say "GS-64 VER=3.00  SC-GS": the SC-88 Pro
	// (rev A) and the headless VE-GS Pro (rev B).
	class Sc88Pro
	{
	public:

		// External interrupt pins as this board wires them. The gate array
		// shares IRQ0 through its own status register; the XP is one line over.
		enum IrqLine : int { IrqGateArray = 0, IrqXp = 1, IrqSubMcu = 2 };

		using SampleFrame = std::pair<int32_t, int32_t>;
		using Lcd = hwLib::Hd44780;
		using Lsp = lspLib::LSPDispatcher;

		static constexpr uint32_t RomSize  = 0x100000;	// 1 MiB
		static constexpr uint32_t SramSize = 0x10000;
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
			GaIrqStatus  = 0xC104,
			GaIrqMask    = 0xC105,
			GaLcdStart   = 0xC11E,
			GaLcdInstr   = 0xC11F,
			GaLcdData    = 0xC120,
			GaLcdDataEnd = 0xC12C,
		};

		explicit Sc88Pro(std::vector<uint8_t> _firmware,
		                 const std::vector<uint8_t>& _waveRom = {},
		                 bool _factoryReset = true);

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

		bool usesSerialMidi() const { return m_serialMidi; }
		size_t midiInBacklog() const { return m_midiInQueue.size() + (m_midiMailboxFull ? 1u : 0u); }

		// Bytes the firmware has transmitted, per SCI channel.
		const std::vector<uint8_t>& serialOut(uint8_t _ch) const { return m_serialOut[_ch & 1]; }

		void setButtons(const uint32_t _b) { m_buttons = _b; }
		uint32_t buttons() const { return m_buttons; }
		uint8_t leds() const { return m_leds; }

		Lcd&       lcd()       { return m_lcd; }
		const Lcd& lcd() const { return m_lcd; }

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
		std::vector<uint8_t> m_sram = std::vector<uint8_t>(SramSize, 0);
		std::vector<uint8_t> m_waveRom;

		xpLib::XP m_xp;
		Lcd m_lcd;
		bool m_valid = false;

		uint64_t m_samplesRendered = 0;
		uint64_t m_cycleTarget = 0;
		uint32_t m_cycleFrac = 0;

		// ---- front panel ----
		uint32_t m_buttons = 0;
		uint8_t  m_scanColumn = 0;

		// ---- gate array ----
		std::array<bool, 4> m_gaInt{};
		uint8_t  m_gaIrqMask = 0x0f;	// 1 = masked
		uint8_t  m_gaIntTrigger = 0;
		uint8_t  m_leds = 0;
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
		std::array<std::vector<uint8_t>, 2> m_serialOut;

	};
}
