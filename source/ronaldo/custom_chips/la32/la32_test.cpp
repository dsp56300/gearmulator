// assert() is this test's only checker; keep it armed in Release builds,
// where CMake's -DNDEBUG would otherwise compile every assertion out.
#undef NDEBUG

#include "la32.h"

#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

namespace
{
	// The scheduling logic as the netlist trace had it, kept as the reference for the rewrite.
	uint8_t netlistInactiveHistory(const uint8_t history, const uint32_t cycle, const la32Lib::Config& config, const bool pitchInactive)
	{
		const uint32_t grouping = config.grouping[(cycle >> 3) & 3];
		const bool w50 = (grouping & 1) != 0;
		const bool w49 = (grouping & 2) != 0;
		bool w51 = true, w52 = true, w53 = true;
		switch (config.scheduleMode)
		{
			case 0: w51 = !w50; w52 = !w49; w53 = true; break;
			case 1: w51 = (config.activeSource & 1) == 0; w52 = (config.activeSource & 2) == 0; w53 = (cycle & 24) == 0; break;
			case 2: w51 = (config.activeSource & 1) == 0; w52 = (config.activeSource & 2) == 0; w53 = (cycle & 8) == 0; break;
			case 3: w51 = (config.activeSource & 1) != 0; w52 = !(((config.activeSource & 1) != 0) ^ ((config.activeSource & 2) != 0)); w53 = (cycle & 16) == 0; break;
		}
		uint32_t w47 = config.activeSource;
		if (w53) w47 = 3;
		uint32_t w48 = w47 ^ 3;
		if (w49) w48 = 0;
		const bool w65 = (cycle & 2) != 0 || (cycle & 1) != 0;
		const bool w66 = (cycle & 4) != 0 || (cycle & 2) != 0 || (cycle & 1) != 0;
		const bool w67 = w47 == 2, w68 = w47 == 3, w69 = w47 == 1;
		const bool w64 = !((w65 && w67) || w68 || ((cycle & 1) != 0 && w69));
		const bool w71 = w51 && !w52, w72 = !w51 && w52, w73 = !w51 && !w52;
		const bool w70 = !((w72 && (cycle & 1) != 0) || (w71 && w65) || (w73 && w66));
		const bool w63 = !((w64 && !w53) || (w70 && w53));
		if (w63) return history;
		uint8_t bit = 0;
		if (w48 == 0) bit |= pitchInactive;
		if (w48 == 1) bit |= (history & 2) != 0;
		if (w48 == 2) bit |= (history & 8) != 0;
		if (w48 == 3) bit |= (history & 128) != 0;
		return uint8_t((history << 1) | bit);
	}

	bool netlistSignFlip(const uint32_t cycle, const uint8_t scheduleMode)
	{
		const uint32_t slot = (cycle + 2) & 31;
		const bool w60 = (slot & 30) != 0;
		const bool w61 = (slot & 16) == 0 || (slot & 14) == 0;
		const bool w59 = !((w60 && w61) || scheduleMode != 3);
		const bool w62 = ((slot & 4) != 0) ^ ((slot & 2) != 0);
		return !((w59 || w62) && (!w59 || (slot & 2) != 0));
	}

	// One word of register block _file for _slot, as the CPU writes it: low byte, then the high byte commits.
	void writeWord(la32Lib::LA32& la32, const unsigned file, const unsigned slot, const uint16_t word)
	{
		const uint32_t offset = file * 0x40 + slot * 2;
		la32.write(offset, static_cast<uint8_t>(word & 0xff));
		la32.write(offset + 1, static_cast<uint8_t>(word >> 8));
	}

	// A square partial as the D-550 starts one (tools/d50/la32_case.py): cutoff FF, pulse width 80,
	// ramp B to D0 at the fastest rate.
	void startSquare(la32Lib::LA32& la32, const unsigned slot, const uint16_t pitch, const unsigned pair, const bool ring)
	{
		writeWord(la32, 1, slot, 0xff80);
		writeWord(la32, 4, slot, static_cast<uint16_t>((ring ? 0x20 : 0) | (pair << 3) | 7));
		writeWord(la32, 0, slot, 0x007f);
		writeWord(la32, 2, slot, 0xd07f);
		writeWord(la32, 3, slot, pitch);
		writeWord(la32, 0, slot, 0x0000);
	}

	// An interpolated PCM partial looping over the first pages of the ROM, ramp A to D0.
	void startInterpolatedPcm(la32Lib::LA32& la32, const unsigned slot, const uint16_t pitch, const unsigned pair, const bool ring)
	{
		writeWord(la32, 1, slot, 0x0000);
		writeWord(la32, 4, slot, static_cast<uint16_t>(0xffc0 | (ring ? 0x20 : 0) | (pair << 3) | 7));
		writeWord(la32, 2, slot, 0xd07f);
		writeWord(la32, 3, slot, pitch);
		writeWord(la32, 0, slot, 0xd07f);
	}

	// A chip with every slot inactive on pair 3 and the modulation words of the D-550 cases.
	void setupRingTest(la32Lib::LA32& la32)
	{
		// Samples that change sign and level, so a PCM partial is not a constant.
		std::vector<uint8_t> rom(1U << 20);
		uint32_t seed = 1;
		for (auto& byte : rom)
		{
			seed = seed * 1664525 + 1013904223;
			byte = static_cast<uint8_t>(seed >> 24);
		}
		la32.setPcmRom(std::move(rom));
		la32.reset();
		la32.write(0x1c1, 0x00);
		for (unsigned slot = 0; slot < 32; ++slot)
		{
			writeWord(la32, 4, slot, 0x1f);
			writeWord(la32, 3, slot, 0xf000);
		}
		static const std::pair<unsigned, uint16_t> modulation[] = {{0, 0x8000}, {1, 0x8000}, {2, 0xfffc}, {5, 0x4000}, {6, 0x4000}, {7, 0x7ffe}};
		for (unsigned group = 0; group < 4; ++group)
			for (const auto& [index, word] : modulation)
				writeWord(la32, 5, group * 8 + index, word);
	}

	// 256 frames once the ramps have settled.
	std::vector<la32Lib::LA32::Outputs> renderSettled(la32Lib::LA32& la32)
	{
		for (int i = 0; i < 64; ++i)
			la32.renderSample();
		std::vector<la32Lib::LA32::Outputs> frames(256);
		for (auto& frame : frames)
			frame = la32.renderSample();
		return frames;
	}

	// Slots 0 and 1 as a ring pair, each on its own output pair, and slot 31 playing on a third.
	std::vector<la32Lib::LA32::Outputs> renderRingPair(const bool evenRing)
	{
		la32Lib::LA32 la32;
		setupRingTest(la32);
		startSquare(la32, 31, 0x8800, 2, false);
		startSquare(la32, 0, 0x9000, 0, evenRing);
		startSquare(la32, 1, 0x9800, 1, true);
		return renderSettled(la32);
	}

	// A square in slot 0 and interpolated PCM in slot 1, each on its own output pair.
	std::vector<la32Lib::LA32::Outputs> renderPcmInOddSlot(const bool pcmRing)
	{
		la32Lib::LA32 la32;
		setupRingTest(la32);
		startSquare(la32, 0, 0x9000, 0, false);
		startInterpolatedPcm(la32, 1, 0x9800, 1, pcmRing);
		return renderSettled(la32);
	}

	// Interpolated PCM in slot 0 and a ring square in slot 1, each on its own output pair.
	std::vector<la32Lib::LA32::Outputs> renderPcmInEvenSlot(const bool pcmRing, const bool pcmPlaying)
	{
		la32Lib::LA32 la32;
		setupRingTest(la32);
		if (pcmPlaying)
			startInterpolatedPcm(la32, 0, 0x9000, 0, pcmRing);
		startSquare(la32, 1, 0x9800, 1, true);
		return renderSettled(la32);
	}

	bool anyNonZero(const std::vector<la32Lib::LA32::Outputs>& frames, const size_t channel)
	{
		for (const auto& frame : frames)
			if (frame[channel] != 0)
				return true;
		return false;
	}
}

int main()
{
	// The rewritten scheduling logic must match the netlist trace on every input.
	for (uint32_t mode = 0; mode < 4; ++mode)
	{
		for (uint32_t cycle = 0; cycle < 32; ++cycle)
			assert(la32Lib::LA32::signFlip(cycle, uint8_t(mode)) == netlistSignFlip(cycle, uint8_t(mode)));
		for (uint32_t source = 0; source < 4; ++source)
			for (uint32_t grouping = 0; grouping < 4; ++grouping)
			{
				la32Lib::Config config;
				config.scheduleMode = uint8_t(mode);
				config.activeSource = uint8_t(source);
				config.grouping.fill(uint8_t(grouping));
				for (uint32_t cycle = 0; cycle < 32; ++cycle)
					for (uint32_t history = 0; history < 256; ++history)
						for (uint32_t pitch = 0; pitch < 2; ++pitch)
							assert(la32Lib::LA32::nextInactiveHistory(uint8_t(history), cycle, config, pitch != 0)
								== netlistInactiveHistory(uint8_t(history), cycle, config, pitch != 0));
			}
	}

	// The unpacked voice registers must carry every bit of the five packed words.
	for (unsigned reg = 0; reg < 5; ++reg)
	{
		for (uint32_t word = 0; word < 0x10000; ++word)
		{
			la32Lib::VoiceRegisters regs;
			la32Lib::LA32::unpackVoiceRegister(regs, reg, static_cast<uint16_t>(word));
			assert(la32Lib::LA32::packVoiceRegister(regs, reg) == word);
		}
	}
	{
		la32Lib::VoiceRegisters regs;
		la32Lib::LA32::unpackVoiceRegister(regs, 4, 0x98d3);
		assert(regs.isPcm && regs.pcm.interpolate && !regs.ring && regs.outputPair == 2 && regs.pan == 3);
		assert(regs.pcm.wave[0].loop && regs.pcm.wave[0].sizeLog2 == 1 && regs.pcm.wave[1].loop && regs.pcm.wave[1].sizeLog2 == 0);
		la32Lib::LA32::unpackVoiceRegister(regs, 4, 0xb553);
		assert(!regs.isPcm && regs.synth.sawtooth && regs.synth.resonance == 21 && regs.synth.resonanceDecay == 5);
		la32Lib::LA32::unpackVoiceRegister(regs, 0, 0xdbff);
		assert(regs.ramp[0].target == 0xdb && regs.ramp[0].down && regs.ramp[0].rate == 0x7f);
		la32Lib::LA32::unpackVoiceRegister(regs, 3, 0x811e);
		assert(regs.octave == 8 && regs.fraction == 0x11e);
	}

	la32Lib::LA32 la32;
	std::vector<uint8_t> rom(1U << 20, 0x80);
	la32.setPcmRom(std::move(rom));
	la32.setRomAddressXor(0x40000);

	bool irq = false;
	la32.setIrqCallback([&irq](const bool _state) { irq = _state; });
	la32.reset();
	la32.write(0x1c1, 0x60);

	for (int i = 0; i < 64; ++i)
	{
		const auto output = la32.renderSample();
		for (const auto sample : output)
			assert(sample >= -32768 && sample <= 32767);
	}

	assert(la32.sh3() == 0);
	assert(!irq);

	// Ring pairs: the odd slot's ring bit puts the product on the bus, the even slot's drops the
	// even slot's own output and nothing else. A ring-only pair must not multiply with slot 31.
	{
		const auto plain = renderRingPair(false);
		const auto ringOnly = renderRingPair(true);
		assert(anyNonZero(plain, 0));			// the even slot is heard with its bit clear
		assert(!anyNonZero(ringOnly, 0));		// and silent with it set, even with slot 31 before it
		assert(anyNonZero(plain, 1) && anyNonZero(plain, 2));
		for (size_t i = 0; i < plain.size(); ++i)
		{
			assert(plain[i][1] == ringOnly[i][1]);	// the odd slot's product does not depend on the even slot's bit
			assert(plain[i][2] == ringOnly[i][2]);	// nor does slot 31
		}
	}

	// Interpolated PCM has no multiplier pass left for a ring product: in the odd slot its ring
	// bit changes nothing. In the even slot it is the modulator of the odd slot like any other
	// partial, and its ring bit drops its own output.
	{
		const auto plain = renderPcmInOddSlot(false);
		const auto ring = renderPcmInOddSlot(true);
		assert(anyNonZero(plain, 1));
		for (size_t i = 0; i < plain.size(); ++i)
			assert(plain[i] == ring[i]);

		const auto heard = renderPcmInEvenSlot(false, true);
		const auto dropped = renderPcmInEvenSlot(true, true);
		const auto absent = renderPcmInEvenSlot(false, false);
		assert(anyNonZero(heard, 0) && !anyNonZero(dropped, 0));
		assert(anyNonZero(heard, 1) && !anyNonZero(absent, 1));	// the product follows the PCM modulator
		for (size_t i = 0; i < heard.size(); ++i)
			assert(heard[i][1] == dropped[i][1]);
	}
	return 0;
}
