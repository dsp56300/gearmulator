#pragma once

#include "88lib/rom/rom.h"

#include <array>
#include <memory>
#include <utility>
#include <vector>

#include "cpu/mcs96/machine.hpp"
#include "custom_chips/lp/lp.h"
#include "custom_chips/rcc/rcc.h"
#include "hardwareLib/hd44780.h"
#include "synthLib/midiBufferParser.h"
#include "synthLib/midiRateLimiter.h"

namespace emu88Lib
{
	class Cm32p
	{
	public:
		using SampleFrame = std::pair<int32_t, int32_t>;
		using WaveRoms = Cm32pRomSet::WaveRoms;
		static constexpr uint32_t ProgramRomSize = Cm32pRomSet::ProgramSize;
		static constexpr uint32_t WaveRomSize = Cm32pRomSet::WaveSize;
		static constexpr uint32_t SampleRate = 32000;
		static constexpr uint32_t CpuStateRate = 12000000 / 3;
		// The card slot's window in the LP's wave space. Cards hold up to its size.
		static constexpr uint32_t CardBase = 0x080000;
		static constexpr uint32_t CardSize = 0x080000;

		explicit Cm32p(const Cm32pRomSet& roms, const std::vector<uint8_t>& card = {});
		// A raw PCM card image (SN-U110 series) descrambled into the slot window, or empty if it
		// is not one: the wrong size, or no tone list in either of the dump orders that circulate.
		static std::vector<uint8_t> decodeCard(const std::vector<uint8_t>& image);
		bool isValid() const { return m_valid; }
		bool hasCard() const { return m_cardInserted; }
		// Front-bezel lamps, bit 0 = MIDI MESSAGE. The firmware pulses HSO.3 low for roughly
		// 45 ms per received message, so a panel poll well under that rate needs no latching.
		uint8_t leds() const;
		const hwLib::Hd44780& lcd() const { return m_serviceLcd; }
		void reset();
		SampleFrame renderSample();
		void addMidiEvent(const synthLib::SMidiEvent& event, uint8_t port = 0);
		void readMidiOut(std::vector<synthLib::SMidiEvent>& events);
		void transportDiscontinuity(uint32_t generation);

	private:
		struct Host final : emu::DeviceLE
		{
			explicit Host(Cm32p& board) : board(board) {}
			uint8_t read8(uint32_t address) override;
			void write8(uint32_t address, uint8_t value) override;
			Cm32p& board;
		};
		static std::vector<uint8_t> decodeWaves(const WaveRoms& waves);
		void writeLcd(bool data, uint8_t value);
		float applyVca();

		mcs96::Machine m_machine{mcs96::Variant::I8x9x};
		std::vector<uint8_t> m_waves;
		lpLib::LP m_lp;
		rccLib::RCC m_rcc;
		hwLib::Hd44780 m_serviceLcd{16, 2};
		Host m_host{*this};
		std::unique_ptr<synthLib::MidiRateLimiter> m_midiIn;
		synthLib::MidiBufferParser m_midiOut{synthLib::MidiEventSource::Device};
		uint64_t m_cycleTarget = 0;
		emu::Scheduler::EventId m_lcdReadyEvent = 0;
		// The VCA's control voltage, the CPU's PWM smoothed by R63/C89, held as the gain it
		// produces. The firmware parks it at full and only sweeps it over the power-on fade.
		float m_vcaGain = 0.0f;
		bool m_valid = false;
		bool m_cardInserted = false;
	};
}
