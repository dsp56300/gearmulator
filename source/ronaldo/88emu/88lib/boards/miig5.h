#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <utility>
#include <vector>

#include "cpu/sh2/machine.hpp"
#include "custom_chips/xp/xp.h"
#include "hardwareLib/tc160g22af.h"
#include "synthLib/midiBufferParser.h"
#include "synthLib/midiRateLimiter.h"

namespace emu88Lib
{
	// MIIG5 mainboard, run as a GM module.
	//
	// SH7042A (33 MHz, 256 KiB on-chip mask ROM) on the cached threaded-code
	// sh2::Machine, a 2 MiB program ROM, two linked XP tone generators on one
	// 32 MiB wave image, and the TC160G22AF gate array - the SC-8850's - which
	// scans the panel, paces the display and multiplexes its sources onto IRQ0.
	// The XPs interrupt on IRQ1 and IRQ2; MIDI in and out is SCI0. The display
	// itself is not modelled: what the firmware writes to it stops at the gate
	// array's registers.
	//
	//   00000000  on-chip ROM                 006C0000  gate array
	//   00200000  XP0 registers, repeating    00800000  battery SRAM, 512 KiB, repeating
	//   00280000  XP1 registers, repeating    00D00000  program ROM, 2 MiB
	//   00400000  CS1 RAM, 4 MiB              01000000  DRAM, 4 MiB
	//
	// There is no panel here. Construction boots the board and presses its keys
	// on a fixed timeline instead: the firmware's own factory reset when asked
	// for, then GM.
	class Miig5
	{
	public:
		using SampleFrame = std::pair<int32_t, int32_t>;

		static constexpr uint32_t CpuRomSize = 0x40000;
		static constexpr uint32_t ProgramRomSize = 0x200000;
		// Decoded: the first 16 MiB on XP chip select 0, the rest on 1.
		static constexpr uint32_t WaveRomSize = 0x2000000;
		static constexpr uint32_t CpuClockHz = 33000000;
		static constexpr uint32_t SampleRate = 32000;

		Miig5(const std::vector<uint8_t>& _cpuRom, const std::vector<uint8_t>& _programRom,
		      std::vector<uint8_t> _waveRom, bool _factoryReset = true);
		~Miig5();

		Miig5(const Miig5&) = delete;
		Miig5& operator=(const Miig5&) = delete;

		bool isValid() const { return m_valid; }
		SampleFrame renderSample();
		void addMidiEvent(const synthLib::SMidiEvent& _event);
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _events);
		void transportDiscontinuity(uint32_t _generation);

	private:
		struct XpDevice final : emu::Device
		{
			explicit XpDevice(xpLib::XP& _xp) : xp(_xp) {}
			uint8_t read8(uint32_t _a) override { return xp.hostRead8(uint16_t(_a & 0x3fff)); }
			void write8(uint32_t _a, uint8_t _v) override { xp.hostWrite8(uint16_t(_a & 0x3fff), _v); }
			uint16_t read16(uint32_t _a) override { return xp.hostRead(uint16_t(_a & 0x3fff)); }
			void write16(uint32_t _a, uint16_t _v) override { xp.hostWrite(uint16_t(_a & 0x3fff), _v); }
			xpLib::XP& xp;
		};
		struct GaDevice final : emu::Device
		{
			explicit GaDevice(Miig5& _board) : board(_board) {}
			uint8_t read8(uint32_t _a) override { return board.gateArrayRead(uint16_t(_a)); }
			void write8(uint32_t _a, uint8_t _v) override { board.m_gateArray.write(uint16_t(_a), _v); }
			Miig5& board;
		};

		void reset();
		void boot(bool _factoryReset);
		void run(uint32_t _samples);
		void tap(uint8_t _code);

		uint8_t gateArrayRead(uint16_t _offset);
		void raiseSource(uint8_t _source);
		void deliverKey();
		bool lcdDmaWanted();
		void cancelEvents();
		static void onTick(void* _self, uint64_t _when, uint64_t _now);
		static void onActiveSensing(void* _self, uint64_t _when, uint64_t _now);
		static void onPanelScan(void* _self, uint64_t _when, uint64_t _now);
		static void onLcdDma(void* _self, uint64_t _when, uint64_t _now);

		// SH7042A in MCU mode 2, on-chip ROM enabled.
		sh2::Machine m_machine{sh2::ChipModel::SH7042, 2};
		std::array<xpLib::XP, 2> m_xp{};
		XpDevice m_xpDevice0{m_xp[0]};
		XpDevice m_xpDevice1{m_xp[1]};
		GaDevice m_gaDevice{*this};
		hwLib::Tc160g22af m_gateArray;
		std::vector<uint8_t> m_waveRom;
		std::unique_ptr<synthLib::MidiRateLimiter> m_midiIn;
		synthLib::MidiBufferParser m_midiOut{synthLib::MidiEventSource::Device};

		uint16_t m_pendingSources = 0;
		// Key codes queue here and go out one at a time: the gate array holds the next one
		// back until the firmware has read the current one.
		std::deque<uint8_t> m_keyEvents;
		bool m_keyBusy = false;
		emu::Scheduler::EventId m_tickEvent = 0;
		emu::Scheduler::EventId m_activeSensingEvent = 0;
		emu::Scheduler::EventId m_panelScanEvent = 0;
		emu::Scheduler::EventId m_lcdDmaEvent = 0;

		uint64_t m_cycleTarget = 0;
		uint32_t m_cycleFraction = 0;
		bool m_valid = false;
	};
}
