#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <utility>
#include <vector>

#include "cpu/sh2/machine7034.hpp"
#include "custom_chips/xp/xp.h"
#include "synthLib/midiBufferParser.h"
#include "synthLib/midiRateLimiter.h"

namespace emu88Lib
{
	// NU-10B mainboard, run as a GM module.
	//
	// SH7034 (20 MHz, 64 KiB on-chip boot ROM) on the cached threaded-code
	// sh2::Machine7034, a 1 MiB program ROM, one XP tone generator on four
	// 2 MiB wave ROMs, and a gate array that scans the panel, paces the display
	// and multiplexes its events and two periodic ticks onto IRQ5. The XP's
	// event output is IRQ7; MIDI in and out is the CPU's own SCI0. The display
	// itself is not modelled: what the firmware writes to it stops at the gate
	// array's registers.
	//
	//   00000000  on-chip ROM                 02300000  work SRAM, 64 KiB
	//   01000000  DRAM, 128 KiB, repeating    02380000  battery SRAM, 64 KiB
	//   02000000  program ROM, 1 MiB          04000000  XP registers
	//   02280000  card window, 512 KiB        04380000  gate array
	//
	// Areas 1-4 repeat at +08000000, the 16-bit shadow most accesses use.
	//
	// There is no panel here. Construction boots the board and presses its keys
	// on a fixed timeline instead: the firmware's own factory reset when asked
	// for, then SHIFT + PERFORM, which selects GM mode.
	class Nu10b
	{
	public:
		using SampleFrame = std::pair<int32_t, int32_t>;

		static constexpr uint32_t CpuRomSize = 0x10000;
		static constexpr uint32_t ProgramRomSize = 0x100000;
		static constexpr uint32_t WaveRomSize = 0x800000;
		static constexpr uint32_t CpuClockHz = 20000000;
		// The XP divides its 24.576 MHz crystal by 768.
		static constexpr uint32_t SampleRate = 32000;

		// _waveRom is the four wave ROMs de-scrambled into one flat image.
		Nu10b(const std::vector<uint8_t>& _cpuRom, const std::vector<uint8_t>& _programRom,
		      std::vector<uint8_t> _waveRom, bool _factoryReset = true);
		~Nu10b();

		Nu10b(const Nu10b&) = delete;
		Nu10b& operator=(const Nu10b&) = delete;

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
			explicit GaDevice(Nu10b& _board) : board(_board) {}
			uint8_t read8(uint32_t _a) override { return board.gateArrayRead(uint8_t(_a & 0x3f)); }
			void write8(uint32_t _a, uint8_t _v) override { board.m_gaRegisters[_a & 0x3f] = _v; }
			Nu10b& board;
		};

		void reset();
		void boot(bool _factoryReset);
		void run(uint32_t _samples);
		void setKey(uint8_t _code, bool _pressed);
		void tap(uint8_t _code);

		uint8_t gateArrayRead(uint8_t _reg);
		void deliverIrq();
		void acknowledgeIrq();
		bool lcdDmaWanted();
		void cancelEvents();
		static void onTimerTick(void* _self, uint64_t _when, uint64_t _now);
		static void onControlTick(void* _self, uint64_t _when, uint64_t _now);
		static void onLcdDma(void* _self, uint64_t _when, uint64_t _now);

		// SH7034 in MCU mode 2, on-chip ROM enabled.
		sh2::Machine7034 m_machine{2};
		xpLib::XP m_xp;
		XpDevice m_xpDevice{m_xp};
		GaDevice m_gaDevice{*this};
		std::vector<uint8_t> m_waveRom;
		std::unique_ptr<synthLib::MidiRateLimiter> m_midiIn;
		synthLib::MidiBufferParser m_midiOut{synthLib::MidiEventSource::Device};

		std::array<uint8_t, 64> m_gaRegisters{};
		// Key codes waiting for the gate array's interrupt, and the ones held down: the
		// firmware's test mode reads the switch matrix rows directly.
		std::deque<uint8_t> m_keyEvents;
		uint32_t m_heldKeys = 0;
		bool m_irqPending = false;
		bool m_timerTickPending = false;
		bool m_controlTickPending = false;
		emu::Scheduler::EventId m_timerTickEvent = 0;
		emu::Scheduler::EventId m_controlTickEvent = 0;
		emu::Scheduler::EventId m_lcdDmaEvent = 0;

		uint64_t m_cycleTarget = 0;
		bool m_valid = false;
	};
}
