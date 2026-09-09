#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <utility>
#include <functional>
#include <vector>

#include "rom.h"
#include "sc88types.h"
#include "sc88_submcu.h"

#include "cpu/h8500/machine.hpp"
#include "hardwareLib/hd44780.h"
#include "custom_chips/xp/xp.h"

#include "synthLib/midiTypes.h"

namespace emu88Lib
{
	using Lcd = hwLib::Hd44780;

	// =====================================================================
	// Sc88 — the SC-88 board
	// =====================================================================
	//
	// H8/510 control CPU, a sub-MCU that owns MIDI and the front panel, a gate
	// array driving the LCD and the LEDs, and the XP tone generator. One object
	// owns the whole machine; the host drives it with renderSample(), one call
	// per audio frame.
	//
	// Memory map (24-bit physical, page = addr >> 16):
	//
	//   page 0   0x0000-0x7FFF   firmware ROM (low 32 KiB)
	//            0x8000-0xFE7F   SRAM
	//            0xFE80-0xFFFF   H8/510 register field (the chip model owns it;
	//                            the port data registers arrive as portRead /
	//                            portWrite, and the bytes above 0xFF20 that no
	//                            register claims are SRAM)
	//   page 1-7                 firmware ROM (addr & 0x7FFFF). ROM 0x8000-0xFFFF
	//                            is blank — the page-0 SRAM overlay hides it.
	//   page 8                   SRAM (addr & 0xFFFF)
	//   page 0xE 0x0000-0x3FFF   XP tone generator register file
	//            0x4000-0xFFFF   SRAM alias (addr & 0xFFFF)
	//   page 0xF                 sub-MCU (0x00xx) and gate array (0xC1xx)
	class Sc88
	{
	public:

		// External interrupt pins as this board wires them.
		enum IrqLine : int { IrqGateArray = 0, IrqXp = 1, IrqSubMcu = 2 };

		using SampleFrame = std::pair<int32_t, int32_t>;	// left, right

		using Model = emu88Lib::Model;	// see sc88types.h

		static constexpr uint32_t RomSize  = 0x80000;	// 512 KiB firmware
		static constexpr uint32_t SramSize = 0x10000;	// 64 KiB work/battery RAM

		// Page 0x0F holds two separate devices.
		//
		// The sub-MCU owns both MIDI DINs and the front-panel matrix and talks
		// to the H8 through a shared RAM window plus a four-byte mailbox.
		enum SubMcuReg : uint16_t
		{
			SmWindowSize  = 0x0100,
			SmSysExStage  = 0x0014,	// staged transfer: a1 a2 a3 data... sum
			SmVersionHi   = 0x00C0,	// reads 0x01
			SmVersionLo   = 0x00C1,	// reads 0x23
			SmStart       = 0x00C2,	// written once at the end of smcu_init
			SmRouting0    = 0x00D0,	// four routing/mode bytes, written at init
			SmRouting3    = 0x00D3,	// and from the OUT/THRU panel page
			SmCommand     = 0x00DC,	// decoded-message opcode; reading acknowledges
			SmChannel     = 0x00DD,	// channel in bits 0-3, input port in 4-5
			SmParam1      = 0x00DE,
			SmParam2      = 0x00DF,
			SmSemaphore   = 0x00FD,	// 0x80 = sub-MCU ready
			SmPanelData   = 0x00FE,	// column out / row in (rows active low)
			SmPanelCtrl   = 0x00FF,	// port direction: 0x08 drive, 0x10 read
		};

		// The gate array is an interrupt controller, the LED port and a
		// burst-mode HD44780 driver.
		enum GateArray : uint16_t
		{
			GaLeds        = 0xC100,	// front-panel LEDs, bit addressed
			GaConfig1     = 0xC101,	// init 0xFE
			GaConfig2     = 0xC102,	// init 0xFF
			GaConfig3     = 0xC103,	// init 0x02
			GaIrqStatus   = 0xC104,	// line that fired, 1-4; reading clears it
			GaIrqMask     = 0xC105,	// 1 = masked; bit n is the line reporting n+1
			GaConfig7     = 0xC107,	// init 0x07
			GaLcdStart    = 0xC11E,	// send the staged burst; bit 0 = data only
			GaLcdInstr    = 0xC11F,	// HD44780 instruction byte (RS=0)
			GaLcdData     = 0xC120,	// data buffer, 13 bytes
			GaLcdDataEnd  = 0xC12C,
		};

		static constexpr uint32_t LcdBurstSize = GaLcdDataEnd - GaLcdData + 1;

		// _firmware = 512 KiB H8/510 control ROM in CPU byte order.
		// _waveRom  = de-scrambled 8 MiB PCM image for the XP (may be empty:
		//             the board still boots, the XP just reads back zeroes).
		// _model should come from Rom::model(), not from a filename or a host
		// flag. It defaults to Sc88 because that is the safe answer for an
		// unrecognised dump — it leaves the VL-only P6DR handling off.
		Sc88(std::vector<uint8_t> _firmware, std::vector<uint8_t> _waveRom = {},
		     Model _model = Model::Sc88, bool _factoryReset = true);
		~Sc88() = default;

		Sc88(const Sc88&) = delete;
		Sc88& operator=(const Sc88&) = delete;

		bool isValid() const { return m_valid; }

		// ---- Audio ----
		// Advance the board by exactly one audio frame and return the stereo
		// output. The H8 runs a fixed cycle budget per frame (Bresenham, so no
		// drift) and the XP renders once.
		SampleFrame renderSample();

		// ---- MIDI ----
		// _port selects the physical input: 0 = MIDI IN A, 1 = MIDI IN B.
		//
		// The SC-88 has two DIN inputs and addresses 32 parts as A01-A16 /
		// B01-B16; which half a message lands on is carried in bits 4-5 of the
		// mailbox channel byte, not in the message itself. The ISR turns those
		// bits into a per-source state index through a ROM table:
		//   00:0bf1  AND.W    #0x0030, r5
		//   00:0bf8  MOV:G.B  @(0x0b82, R5), R5   ; 0x00 -> 0, 0x10 -> 2, 0x20 -> 4
		// and bit 4 additionally picks which byte of the part-group word at SRAM
		// 0x06EE gets ORed back into the record (00:0c04..00:0c12).
		enum MidiPort : uint8_t { MidiInA = 0, MidiInB = 1 };
		void addMidiEvent(const synthLib::SMidiEvent& _event, uint8_t _port = MidiInA);

		// Post a raw mailbox record, bypassing the host-side parser. Used to
		// probe which opcodes the firmware acts on.
		void postRawMidiMessage(uint8_t _command, uint8_t _channel, uint8_t _p1, uint8_t _p2);
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _events);

		// ---- Front panel ----
		void setButton(Button _button, bool _pressed);
		void setButton(uint32_t _index, bool _pressed);
		void setButtons(uint32_t _bitmap) { m_buttons = _bitmap; }
		uint32_t buttons() const { return m_buttons; }

		// Front-panel LED port (gate array 0xC100), bit addressed. The firmware
		// keeps a shadow of it at SRAM 08:54C7/54C8.
		uint8_t leds() const { return m_leds; }

		// The H8's four analog inputs, 10 bit left-justified in a 16-bit word as
		// the ADC result registers present them. 01:6993 converts all four in
		// scan mode and copies ADDRA-ADDRD to the caller's buffer; boot lands
		// them at 08:FE30-08:FE37 and the factory battery test at 08:5074+.
		//
		// Channel 1 is the one the firmware acts on: 00:0AF1 takes its high
		// byte and derives the gate byte at 08:FE2E from it.
		void     setAnalog(uint8_t _ch, uint16_t _v) { if(_ch < 4) m_analog[_ch] = _v; }
		uint16_t analog(uint8_t _ch) const { return _ch < 4 ? m_analog[_ch] : 0; }

		// P5DR configuration straps. Read once at boot; bit 0 selects the mixer
		// send-3 law and the effects DSP image, bits 2-4 the DSP config word.
		// Must be set before the first renderSample to have any effect.
		void    setStraps(uint8_t _v) { m_p5dr = _v; }
		uint8_t straps() const { return m_p5dr; }

		// The four sub-MCU routing/mode bytes at 0F:00D0-00D3. Written at init
		// and by the OUT/THRU panel page; the encoding is not decoded yet, but
		// this is where MIDI out and THRU are configured.
		std::array<uint8_t, 4> midiRouting() const
		{
			return { m_subMcuRam[SmRouting0 + 0], m_subMcuRam[SmRouting0 + 1],
			         m_subMcuRam[SmRouting0 + 2], m_subMcuRam[SmRouting0 + 3] };
		}

		// If this grows, the firmware is not consuming as fast as the host is
		// delivering.
		size_t midiInBacklog() const { return m_midiInQueue.size(); }

		Lcd&       lcd()       { return m_lcd; }
		const Lcd& lcd() const { return m_lcd; }
		bool lcdEnabled() const { return m_lcdEnabled; }

	public:
		// Elapsed phi states.
		uint64_t cycles() const { return m_machine.now(); }
		void     reset() { powerCycle(); }

	protected:
		// ---- Board hooks, called from the chip model ----
		// The external bus, page by page (the register field never reaches
		// these — the chip decodes it on-chip).
		uint8_t  extRead8 (uint32_t _addr);
		void     extWrite8(uint32_t _addr, uint8_t _val);
		// A port data-register read, with the value the port model computed
		// from its latch and pin levels.
		uint8_t portRead (uint32_t _addr, uint8_t _value);
		void     portWrite(uint32_t _addr, uint8_t _val);
		void uartTransmit(uint8_t _byte);
		void uart2Transmit(uint8_t _byte);
		// A byte arriving on the RXD pin of SCI1 / SCI2.
		void     uartReceive (uint8_t _byte) { m_machine.sci(0).receive_byte(_byte); }
		void     uart2Receive(uint8_t _byte) { m_machine.sci(1).receive_byte(_byte); }
		// Drive one of the board's external interrupt pins (IrqLine); a
		// negative line is disabled.
		void     requestIrq(int _line, bool _level)
		{
			if(_line >= 0)
				m_machine.intc().set_irq_pin(static_cast<unsigned>(_line), _level);
		}

	protected:
		SampleFrame renderXpAudioFrame() const;

		uint8_t gateArrayRead (uint16_t _addr);
		void gateArrayWrite(uint16_t _addr, uint8_t _val);

		// Raise gate-array line _line; the CPU sees it as IRQ0 and reads
		// GaIrqStatus to find out which line it was. Edge-triggered: the
		// firmware's LCD-ready path pulses low then high.
		void    setGateArrayInt(uint8_t _line, bool _level);

		uint8_t subMcuRead(uint16_t _addr);
		void    subMcuWrite(uint16_t _addr, uint8_t _val);
		void    lcdSendBurst(bool _dataOnly);

		// Hand the next queued MIDI message to the mailbox if it is free.
		void    pumpMidiIn();

		// Stage a SysEx message into the shared RAM and post its mailbox record.
		void    addSysEx(const synthLib::SysexBuffer& _sysex, uint8_t _port);

		void    runFactoryReset();
		void    powerCycle();

		// ---- Chip ----
		//
		// H8/510 in expanded maximum mode with a 16-bit external bus (mode 4).
		// The board's pages reach it as bus devices; page 0 below 0x8000 and
		// pages 1-7 are the mask ROM, mapped straight into the bus image so
		// the core fetches and caches from it.
		struct PageDevice final : h8500::Device
		{
			PageDevice(Sc88& _b, uint32_t _page) : board(_b), page(_page) {}
			uint8_t read8(uint32_t _a) override { return board.extRead8((page << 16) | (_a & 0xffff)); }
			void write8(uint32_t _a, uint8_t _v) override { board.extWrite8((page << 16) | (_a & 0xffff), _v); }
			Sc88& board;
			uint32_t page;
		};
		// Register-field bytes no on-chip register claims: 0xFF20 and up is
		// board SRAM, the reserved registers below it read as ones.
		struct RegFieldTail final : h8500::Device
		{
			explicit RegFieldTail(Sc88& _b) : board(_b) {}
			uint8_t read8(uint32_t _a) override
			{
				return (_a & 0xffff) >= 0xff20 ? board.extRead8(_a & 0xffff) : 0xff;
			}
			void write8(uint32_t _a, uint8_t _v) override
			{
				if((_a & 0xffff) >= 0xff20) board.extWrite8(_a & 0xffff, _v);
			}
			Sc88& board;
		};

		h8500::Machine m_machine {h8500::ChipModel::H8_510, 4};
		PageDevice     m_page0 {*this, 0x0};
		PageDevice     m_page8 {*this, 0x8};
		PageDevice     m_pageE {*this, 0xe};
		PageDevice     m_pageF {*this, 0xf};
		RegFieldTail   m_regFieldTail {*this};

		// Set up the bus map and the chip's host hooks. Runs once, from the
		// constructor.
		void wireChip();
		// One audio frame's CPU budget, stepped instruction by instruction so

		// ---- Storage ----
		std::vector<uint8_t> m_rom;
		std::vector<uint8_t> m_sram = std::vector<uint8_t>(SramSize, 0);
		std::vector<uint8_t> m_waveRom;

		xpLib::XP m_xp;
		bool m_xpEnabled = true;

		// Columns 0..19 are text and 20..23 carry CGRAM glyphs, observed by
		// booting the firmware -- 25 visible columns in total.
		//
		// The panel does NOT display these as two straight lines: the glass
		// scatters them into a 19-character instrument line, six 3-character
		// numeric readouts, two level meters (the CGRAM cells) and an L/R lamp.
		// The authoritative mapping lives in 88emuplayer/panel.hpp. The play
		// screen's raw DDRAM is
		//   "A01001 Piano 1      <mtr>"
		//   "100  0  0 40  0A01  <mtr>"
		// which the panel renders as A01 / 001 Piano 1, LEVEL 100, PAN 0,
		// REVERB 40, CHORUS 0, K SHIFT +0, MIDI CH A01.
		//
		// The renderers read getDdRam() directly and do their own mapping, so
		// the visible-window accessors are not used here.
		Lcd m_lcd{25, 2};

		Model m_model = Model::Sc88;
		bool m_valid = false;
		bool m_lcdEnabled = true;

		// ---- CPU pacing ----
		uint64_t m_samplesRendered = 0;
		uint64_t m_cycleTarget = 0;
		uint32_t m_cycleFrac = 0;

		// ---- Front panel ----
		uint32_t m_buttons = 0;		// active-high bitmap, bit = Button index
		uint8_t  m_scanColumn = 0;	// last value written to SmPanelData
		uint8_t  m_panelCtrl  = 0;	// last value written to SmPanelCtrl
		uint8_t  m_leds       = 0;	// GaLeds
		uint8_t  m_p5dr;			// configuration straps, see g_p5drStraps
		std::array<uint16_t, 4> m_analog{};

		// ---- Gate array interrupt controller ----
		//
		// Four lines. GaIrqStatus reports line n as the value n+1, and the mask
		// bit for that line is bit n -- so the two are offset by one, which is
		// easy to get wrong. Only line 0 (status 1, "LCD burst finished") is
		// ever enabled by this firmware.
		std::array<bool, 4> m_gaInt{};
		uint8_t m_gaIrqMask = 0x0f;	// 1 = masked, as ga_initIrq leaves it
		uint8_t m_gaIntTrigger = 0;	// pending status value, 0 = none

		// ---- LCD burst ----
		//
		// The gate array is not a byte-at-a-time HD44780 port. The firmware
		// stages an instruction byte and up to 13 characters, then writes
		// GaLcdStart to send the lot; bit 0 of that write means "data only, no
		// instruction". m_lcdStaged is how many data bytes have been written
		// since the last burst.
		uint8_t  m_lcdInstr = 0;
		uint8_t  m_lcdStaged = 0;
		std::array<uint8_t, LcdBurstSize> m_lcdBuffer{};
		emu::Scheduler::EventId m_gaLcdEvent = 0;	// pending LCD-ready pulse

		// ---- MIDI in ----
		//
		// The wire-level half of the MIDI controller MCU lives in Sc88SubMcu: it
		// parses the raw byte streams (running status, realtime interleave,
		// SysEx framing and checksum, per-source state) and emits mailbox
		// records. This class keeps the board half — the shared RAM, the
		// mailbox registers, IRQ2 and the wire pacing.
		using MidiMessage = Sc88SubMcu::Record;

		// Sub-MCU mailbox opcodes. Channel voice messages are the MIDI status
		// nibble biased by 7; the 0xE_ family is system messages, whose payload
		// is passed by pointer into the shared RAM rather than in the mailbox.
		enum MidiOpcode : uint8_t
		{
			OpNoteOff        = 1,
			OpNoteOn         = 2,
			OpPolyPressure   = 3,
			OpControlChange  = 4,
			OpProgramChange  = 5,
			OpChannelPressure= 6,
			OpPitchBend      = 7,

			// A Roland DT1 has no opcode of its own: the sub-MCU decodes it and
			// posts a bulk record whose opcode is the transfer's address high
			// byte (see addSysEx). 0xe0 is NOT that — its handler at 00:1822
			// only posts a notification, which is where the "Check Sum Error"
			// message comes from.
			OpSysExContinue  = 0xe7,	// next chunk of a split bulk transfer
			OpSysExError     = 0xe0,	// sub-MCU -> CPU: bad SysEx, param1 = code
			OpSysExUniNonRt  = 0xee,	// F0 7E ... — GM System On, param1 = sub-ID2
			OpSysExUniRt     = 0xef,	// F0 7F ... — Master Volume, param1 = MSB
		};

		// Shared RAM window. SysEx bodies are staged here and the main CPU
		// copies them out itself.
		static constexpr uint16_t SysExStageOffset = 0x10;	// clear of the mailbox

		std::array<uint8_t, SmWindowSize> m_subMcuRam{};
		bool m_subMcuStarted = false;	// set when the CPU writes SmStart

		// MIDI-in wire pacing. The sub-MCU receives at 31250 baud, 10 bits per
		// byte, so a byte takes g_sampleRate*10/31250 samples to arrive. Without
		// this the mailbox delivers a message every audio sample — about 32x
		// wire rate — which overruns the firmware's 64-record ring buffer
		// (SRAM 0x05EE-0x06EE) and pushes it down its overflow path.
		static constexpr uint32_t MidiBitsPerByte = 10;
		static constexpr uint32_t MidiBaud        = 31250;

		uint32_t m_midiWireDelay = 0;	// samples until the next byte has arrived
		uint32_t m_midiWireFrac  = 0;

		std::deque<MidiMessage> m_midiInQueue;
		MidiMessage m_midiMailbox;
		bool        m_midiMailboxFull = false;

		Sc88SubMcu m_subMcu{[this](MidiMessage&& _r) { m_midiInQueue.push_back(std::move(_r)); },
		                     SysExStageOffset,
		                     static_cast<uint16_t>(std::min<size_t>(SmCommand, 0x80) - SysExStageOffset)};

		std::vector<synthLib::SMidiEvent> m_midiOut;

		// ---- Editor transport ----
	};
}
