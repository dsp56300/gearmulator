#pragma once

#include "88lib/boards/mv30types.h"
#include "88lib/rom/rom.h"

#include <array>
#include <bitset>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "cpu/mcs96/machine.hpp"
#include "custom_chips/hg62e33b08f/hg62e33b08f.h"
#include "custom_chips/lp/lp.h"
#include "custom_chips/rcc/rcc.h"
#include "custom_chips/tvf/tvf_emulator.h"
#include "hardwareLib/floppyImage.h"
#include "hardwareLib/t6963c.h"
#include "hardwareLib/upd765.h"
#include "synthLib/midiBufferParser.h"
#include "synthLib/midiRateLimiter.h"

namespace emu88Lib
{
	// =====================================================================
	// Mv30 - the Roland MV-30 Studio M main board
	// =====================================================================
	//
	// An 80C196KB at 12 MHz behind the HG62E33B08F CPU gate array, which banks
	// 512 KiB of DRAM, the 16 KiB boot ROM and a peripheral page into the CPU's
	// four windows, separately for instruction fetches and data. The sound
	// section is the D-70's: LP (MB87419/MB87420) -> TVF (MB87424) -> RCC
	// (TC23SC140) -> DAC, with the D-70's six wave ROMs. The operating system
	// is not in ROM: the boot ROM reads it off the system disk into RAM through
	// the uPD72068 floppy controller and the gate array's DMA channel. Here the
	// system disk is part of the ROM set and stays in the drive, read only, and
	// the board runs that real boot - at full speed, before it is handed out
	// for audio (bootOffline()).
	//
	// Memory map, through the gate array's banks (physical = bank * 0x400 + offset):
	//   bank 0x000-0x3FF  DRAM, 512 KiB fitted (banks 0x000-0x1FF)
	//   bank 0x400-0x77F  peripheral page; the boot ROM selects bank 0x474,
	//                     and the offset below is relative to that
	//   bank 0x780-       boot ROM, A14/A15 not connected (16 KiB mirrored)
	//
	// Peripheral page:
	//   +0000-07FF  RCC
	//   +0800/0801  TC23SC060 panel: register index / data
	//   +1000/1001  uPD72068: status (write: aux command) / FIFO
	//   +1800-18FF  LP
	//   +2000-201F  MN53015 tape sync (not modelled)
	//   +2800/2802  T6963C LCD (240 x 64): data / status-command
	//   +3000-30FF  TVF
	//
	// CPU pins: EXTINT (P0.7) is the LP's interrupt, EXTINT1 (P2.2) the gate
	// array's. P0.5/P0.6 are the value encoder's direction / event latch,
	// cleared by HSO2 low. ACH0 reads the fader selected by P1.0-2; P1.3 mutes
	// the line outputs. P2.3 is the drive's DSKCHG, P2.5 the firmware's TC to
	// the FDC. HSI0 is the footswitch, HSI1/HSI2 the card B/A sense (high =
	// empty). MIDI IN/OUT is the CPU's own serial port.
	//
	// Register map and wiring after the MAME driver (roland_mv30.cpp), which
	// took them from the boot ROM, the system disk and the service notes.
	//
	// PCM cards: both sockets share the wave ROMs' address and data wiring and
	// select the two holes in the LP's space, card A at 0x280000 and card B at
	// 0x380000; an inserted card pulls its sense line (HSI2 / HSI1) low. They take
	// the U-110's SN-U110 cards, as the CM-32P does, decoded the same way. Card A
	// is offered here.
	//
	// Not modelled yet: the tape sync chip, the metronome oscillator and the
	// analog output stage.
	class Mv30
	{
	public:
		using SampleFrame = std::pair<int32_t, int32_t>;

		static constexpr uint32_t CpuClock = 12000000;
		static constexpr uint32_t CpuStateRate = CpuClock / 2;	// one state per two oscillator clocks
		static constexpr uint32_t StatesPerMicrosecond = CpuStateRate / 1000000;
		static constexpr uint32_t SampleRate = 32000;
		static constexpr uint32_t RamSize = 0x80000;
		static constexpr uint32_t FaderMax = 0x3ff;

		static constexpr uint32_t CardABase = 0x280000;
		static constexpr uint32_t CardBBase = 0x380000;

		// _card is a raw PCM card image for slot A (see Cm32p::decodeCard); empty leaves it empty.
		explicit Mv30(const Mv30RomSet& _roms, const std::vector<uint8_t>& _card = {});

		Mv30(const Mv30&) = delete;
		Mv30& operator=(const Mv30&) = delete;

		bool isValid() const { return m_valid; }
		bool hasCard() const { return m_cardInserted; }
		// Power-on with the system disk in the drive, followed by bootOffline() unless
		// `_bootOffline` is false.
		void reset(bool _bootOffline = true);

		// Run the boot ROM's disk load and the OS start without producing audio, as fast
		// as the host allows, until the OS takes panel input. About 19 s of emulated time:
		// the boot ROM waits 4.6 s before it reads the disk, the load takes 6, and the OS
		// probes RAM and waits again before it enables the panel scan. Returns false if the
		// OS was not up after `_maxSeconds` of emulated time.
		bool bootOffline(float _maxSeconds = 40.0f);
		// The last bootOffline() got the OS up.
		bool booted() const { return m_booted; }
		// The boot ROM has handed over to the OS loaded from disk.
		bool osRunning() const;
		// The OS has finished starting and takes MIDI: it turned the panel's key scan and the MIDI receiver on
		// and entered a screen. In the start-up mode before, it drops what it receives - with running status,
		// every message after a lost status byte too.
		bool osReady() const;

		// ---- Audio ----
		SampleFrame renderSample();
		uint64_t samplesRendered() const { return m_samplesRendered; }

		// ---- MIDI, through the CPU's serial port ----
		void addMidiEvent(const synthLib::SMidiEvent& _event);
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _events);
		void transportDiscontinuity(uint32_t _generation);

		// ---- Front panel ----
		void setButton(Mv30Button _button, bool _pressed);
		bool isButtonPressed(Mv30Button _button) const;
		// Every switch at once, bit n = Mv30Button n
		void setButtons(uint64_t _buttons);
		// Value encoder, in detents (positive = clockwise).
		void turnDial(int _detents);
		// Track faders 1-8, 0..FaderMax.
		void setFader(unsigned _index, uint16_t _value);
		void setFootswitch(bool _pressed);
		// The TC23SC060's LED words: index 0-7 at registers 08-0F, 8-15 at 10-17, one
		// per scan row. Set bits light.
		uint8_t ledRegister(unsigned _index) const { return m_panel[0x08 + (_index & 15)]; }
		uint8_t panelRegister(uint8_t _reg) const { return m_panel[_reg & 0x7f]; }

		// ---- The OS's song memory ----
		// The current song's part settings and timbre names, see Mv30Parts. Invalid until osReady().
		void readParts(Mv30Parts& _parts) const;
		bool outputMuted() const { return m_outputMute; }

		// ---- Display ----
		const hwLib::T6963C& lcd() const { return m_lcd; }
		hwLib::T6963C& lcd() { return m_lcd; }

		// ---- Debugging ----
		mcs96::Machine& machine() { return m_machine; }
		const hwLib::Upd765& fdc() const { return m_fdc; }
		hwLib::Upd765& fdc() { return m_fdc; }
		const hg62e33b08fLib::HG62E33B08F& gate() const { return m_gate; }
		const std::vector<uint8_t>& ram() const { return m_ram; }
		const hwLib::FloppyImage& systemDisk() const { return m_disk; }

	private:
		struct BoardDevice final : emu::DeviceLE
		{
			explicit BoardDevice(Mv30& _board) : board(_board) {}
			uint8_t read8(uint32_t _a) override { return board.read8(static_cast<uint16_t>(_a)); }
			void write8(uint32_t _a, uint8_t _v) override { board.write8(static_cast<uint16_t>(_a), _v); }
			uint16_t read16(uint32_t _a) override { return board.read16(static_cast<uint16_t>(_a)); }
			Mv30& board;
		};

		static std::vector<uint8_t> decodeWaves(const Mv30RomSet::WaveRoms& _waves);

		// CPU bus
		uint8_t read8(uint16_t _address);
		void write8(uint16_t _address, uint8_t _value);
		uint16_t read16(uint16_t _address);
		uint8_t readMemory(uint16_t _address, bool _fetch);
		void writeMemory(uint16_t _address, uint8_t _value);
		void noteRamWrite(uint32_t _physical);

		// Peripheral page
		uint8_t peripheralRead(uint16_t _offset);
		void peripheralWrite(uint16_t _offset, uint8_t _value);
		uint8_t lpRead(uint8_t _offset);

		// TC23SC060
		uint8_t panelRead(uint8_t _reg);
		void panelWrite(uint8_t _reg, uint8_t _value);
		void panelTick();
		void updatePanelIrq();

		// CPU pins
		void onPort1(uint8_t _value);
		void onPort2(uint8_t _value);
		void onHso(uint8_t _value);
		void updatePort0();
		void updatePort2();
		void onFdcDrq(bool _level);

		void runCpuForSample();
		SampleFrame renderAudioFrame();

		bool m_valid = false;
		bool m_booted = false;
		bool m_cardInserted = false;
		mcs96::Machine m_machine{mcs96::Variant::I80C196KB};
		BoardDevice m_boardDevice{*this};
		hg62e33b08fLib::HG62E33B08F m_gate;

		std::vector<uint8_t> m_boot;
		std::vector<uint8_t> m_ram = std::vector<uint8_t>(RamSize, 0);
		// RAM lines (128 bytes) instructions have been decoded from: a write to one of them
		// drops the decoded code.
		std::bitset<RamSize / 128> m_codeLines;

		std::vector<uint8_t> m_waves;
		lpLib::LP m_lp{m_waves};
		tvfLib::TVFEmulator m_tvf{true};
		rccLib::RCC m_rcc;
		std::array<int32_t, 4> m_rccSerialDelay{};
		uint8_t m_lpRegisters[0x100]{};

		hwLib::FloppyImage m_disk;
		hwLib::Upd765 m_fdc{StatesPerMicrosecond, 250000};
		// The drive's DSKCHG: set by an insert, cleared by the first head step with a disk in.
		// A disk that is in at power-on does not raise it; the boot ROM shows "insert disk"
		// and waits while it is set.
		bool m_diskChanged = false;
		bool m_tcLine = false;

		hwLib::T6963C m_lcd;

		// TC23SC060
		std::array<uint8_t, 0x80> m_panel{};
		uint8_t m_panelIndex = 0;
		std::array<uint8_t, 8> m_keys{};
		std::array<uint8_t, 8> m_keysPrevious{};
		std::array<uint8_t, 16> m_keyFifo{};
		uint8_t m_keyHead = 0, m_keyCount = 0;
		bool m_panelIrq = false;
		uint32_t m_panelTickPhase = 0;
		// The OS turned the panel scan on, and the sample it did
		bool m_osReady = false;
		uint64_t m_osReadySample = 0;

		// Faders, encoder latch (IC14), footswitch
		std::array<uint16_t, 8> m_faders{};
		uint8_t m_faderSelect = 0;
		int m_dialPending = 0;
		bool m_encoderEvent = false, m_encoderNegative = false, m_encoderClear = false;
		bool m_lpIrq = false;
		bool m_outputMute = true;
		uint8_t m_hso = 0;

		std::unique_ptr<synthLib::MidiRateLimiter> m_midiIn;
		synthLib::MidiBufferParser m_midiOut{synthLib::MidiEventSource::Device};

		uint64_t m_samplesRendered = 0;
		uint64_t m_cycleTarget = 0;
		uint32_t m_cycleFraction = 0;
	};
}
