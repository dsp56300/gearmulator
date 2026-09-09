/*
 * gpLib — Roland GP tone generator. Derived from Nuked-SC55's pcm.cpp,
 * Copyright (C) 2021, 2024 nukeykt, GPL-2.0-or-later; see gp.cpp.
 */
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace gpLib
{
	constexpr uint32_t ChannelCount = 32;
	constexpr uint32_t DspChannelFirst = 28;	// channels 28-31 belong to the effects section
	constexpr uint32_t VoiceChannels = DspChannelFirst;
	constexpr uint32_t EramWords = 0x4000;

	struct GpConfig
	{
		// The GP's own clock, NOT an audio rate. The chip spends 25 of these
		// per voice time slot, so the audio rate falls out of this and the
		// programmed voice count — see GP::iterationRate(). 24 MHz on the
		// SC-55mk2, which is where its 66207 Hz DAC rate comes from.
		uint32_t chipClockHz = 24000000;
		// First-generation silicon (the SC-55 mk1's) runs the filter inside
		// the 20-bit saturating datapath; later revisions carry the
		// intermediates at full width.
		bool saturatingFilter = false;
	};

	class GP
	{
	public:
		using SampleFrame = std::pair<int32_t, int32_t>;	// left, right

		static constexpr uint32_t RegWindowSize = 0x40;
		static constexpr uint32_t BankCount = 8;	// wave-ROM chip selects

		// Output scale: renderFrame() returns values where +/- (1 << 21) is
		// full scale, matching the convention xpLib uses so a board can mix
		// the two without a second scaling law. The DAC word is 20 bits with
		// a nominal full scale of 2^18, hence the <<3 on the way out.
		static constexpr int32_t OutputFullScale = 1 << 21;

		// The chip's register files and working state. RAM1 holds 20-bit
		// words and RAM2 16-bit words; six and twelve per channel are
		// reachable, the rest is padding.
		struct Regs
		{
			uint32_t ram1[32][8];
			uint16_t ram2[32][16];
			uint32_t select_channel;
			uint32_t voice_mask;
			uint32_t voice_mask_pending;
			uint32_t voice_mask_updating;
			uint32_t write_latch;
			uint32_t wave_read_address;
			uint8_t wave_byte_latch;
			uint32_t read_latch;
			uint8_t config_reg_3c; // SC55: 0xc3
			uint8_t config_reg_3d;
			uint32_t irq_channel;
			uint32_t irq_assert;

			uint32_t nfs;	// "not first sample": gates all state write-back on the first iteration

			uint32_t tv_counter;

			uint16_t eram[EramWords];

			int accum_l;
			int accum_r;
			int rcsum[2];
		};

		explicit GP(GpConfig _config = GpConfig{});

		// Clears every register and the working state; installed wave ROMs
		// survive, as they would a chip reset.
		void reset();

		const GpConfig& config() const { return m_config; }

		// ---- Host bus ----
		// `_addr` is masked to the 6-bit window, so a board can pass the raw
		// decoded address through. Reads have side effects (status reads
		// drop the interrupt, a mask read commits the pending mask).
		uint8_t read8(uint32_t _addr);
		void    write8(uint32_t _addr, uint8_t _val);

		// ---- Wave ROM ----
		// Banks are the chip selects the address decoder produces. Boards
		// install whichever they populate, each sized to the chip fitted
		// (a power of two: the chip wraps within its own size); unfitted
		// banks read 0.
		void setWaveRom(uint8_t _bank, std::vector<uint8_t> _data);
		bool hasWaveRom(uint8_t _bank) const;

		// Read one byte the way the chip's own address decoder would, i.e.
		// through the bank shift selected by config B bit 5.
		uint8_t waveRomRead(const uint32_t _addr) const { return readRom(_addr); }

		// ---- Interrupt ----
		// The chip raises a line when a voice reaches the end of its sample.
		// The host clears it by reading the status at 0x3E.
		void setIrqCallback(std::function<void(bool)> _cb) { m_irqCallback = std::move(_cb); }
		bool irqLevel() const { return m_irqLevel; }

		// Raise the voice-end interrupt for `_channel`, as the voice engine
		// does; exposed so a board can drive the handshake end to end.
		void raiseVoiceEndIrq(uint8_t _channel);

		// ---- Audio ----
		// Advance the chip by one DAC output sample and return it.
		//
		// The chip runs its voice pass once per ITERATION and its DAC emits
		// one or two samples from it: config A bit 6 turns on 2x
		// oversampling, and then every other call just clocks the second
		// sub-sample out of the noise shaper without re-running the voices.
		// So the output rate is one or two times the iteration rate — see
		// iterationRate() / dacRate().
		SampleFrame renderFrame();

		// How many of the chip's own clocks one iteration takes: 25 per voice
		// time slot plus one spare slot. This is what ties the audio rate to
		// the crystal — an SC-55mk2 clocks the GP at 24 MHz with 28 voices,
		// giving 24e6 / ((28+1)*25) = 33103.4 iterations/s and, oversampled,
		// 66206.9 samples/s.
		uint32_t clocksPerIteration() const { return (voiceSlots() + 1) * 25; }
		double   iterationRate() const
		{
			return static_cast<double>(m_config.chipClockHz) / clocksPerIteration();
		}
		double   dacRate() const { return iterationRate() * (oversampling() ? 2 : 1); }
		bool     oversampling() const { return (m_regs.config_reg_3c & 0x40) != 0; }

		// ---- Inspection ----
		const Regs& regs() const { return m_regs; }
		uint8_t  selectedChannel() const { return static_cast<uint8_t>(m_regs.select_channel); }
		uint32_t voiceSlots() const { return (m_regs.config_reg_3d & 31) + 1; }

	private:
		// The six effect-return pairs the microprogram produces per
		// iteration: a mix contribution and a send-sum contribution each,
		// folded in by the voice pass at fixed time slots.
		struct EffectReturns
		{
			int32_t mix[6];
			int32_t send[6];
		};

		void runIteration();
		static uint32_t ram1Slot(uint32_t _address);
		static uint32_t ram2Slot(uint32_t _address);
		uint8_t readRom(uint32_t _address) const;
		// The per-iteration cached form of readRom().
		uint8_t fetchRom(const uint32_t _address) const
		{
			const uint32_t bank = (_address >> m_bankShift) & 7;
			const uint8_t* data = m_bankData[bank];
			return data ? data[_address & m_bankMask[bank]] : 0;
		}

		void prepareEnvelopeClock();
		void dacStage();
		void calcTv(int e, int adjust, uint16_t* levelcur, int active, int* volmul) const;
		int32_t eramUnpack(int32_t addr, int32_t type = 0) const;
		void eramPack(int32_t addr, int32_t val);
		void effectsSection(EffectReturns& _returns);
		void chorusLfo();
		void voicePass(const EffectReturns& _returns);
		uint32_t renderVoice(uint32_t _slot, int key);
		uint32_t silentVoice(uint32_t _slot);
		// The chip's two outputs: the interrupt line and the DAC word pair.
		void driveIrq(bool _level);
		void emitSample(int32_t _left, int32_t _right);

		GpConfig m_config;
		Regs m_regs{};
		std::array<std::vector<uint8_t>, BankCount> m_waveRom;

		// Per-iteration caches, rebuilt at the top of runIteration().
		std::array<uint32_t, 5> m_tvAddLow{};
		std::array<uint32_t, 5> m_tvWrite{};
		std::array<const uint8_t*, BankCount> m_bankData{};
		std::array<uint32_t, BankCount> m_bankMask{};
		uint32_t m_bankShift = 19;

		std::array<SampleFrame, 2> m_dacOut{};
		uint32_t m_emitted = 0;		// DAC words emitted by the current iteration
		uint32_t m_dacPhase = 0;
		bool m_irqLevel = false;
		std::function<void(bool)> m_irqCallback;
	};
}
