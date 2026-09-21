// assert() is this test's only checker; keep it armed in Release builds,
// where CMake's -DNDEBUG would otherwise compile every assertion out.
#undef NDEBUG

#include "la32.h"

#include <cassert>
#include <cstdint>
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
	return 0;
}
