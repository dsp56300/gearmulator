#include "lp.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

namespace
{
	void require(const bool _condition, const char* _message)
	{
		if (_condition) return;
		std::fprintf(stderr, "lp test failed: %s\n", _message);
		std::exit(1);
	}

	template <typename Generator>
	uint64_t curveHash(Generator&& _generator)
	{
		uint64_t hash = 1469598103934665603ull;
		for (unsigned i = 0; i < 256; ++i)
		{
			hash ^= static_cast<uint32_t>(_generator(static_cast<uint8_t>(i)));
			hash *= 1099511628211ull;
		}
		return hash;
	}

	uint64_t interpolationRomHash()
	{
		uint64_t hash = 1469598103934665603ull;
		for (unsigned tap = 0; tap < 3; ++tap)
		{
			for (unsigned phase = 0; phase < 128; ++phase)
			{
				hash ^= static_cast<uint32_t>(lpLib::LP::interpolationWeight(tap, phase));
				hash *= 1099511628211ull;
			}
		}
		return hash;
	}

	void programImmediateEnvelope(lpLib::LP& _lp, const uint8_t _voice)
	{
		_lp.write(0x1f, _voice);
		_lp.write(0x03, 0x00);
		_lp.write(0x02, 0x00);
		_lp.write(0x07, 0x30);
		_lp.write(0x06, 0x7f);
	}

	void writeCurrentVolume(lpLib::LP& _lp, const uint8_t _voice, const uint32_t _volume)
	{
		_lp.write(0x1f, _voice);
		_lp.write(0x03, static_cast<uint8_t>(_volume >> 24));
		_lp.write(0x02, static_cast<uint8_t>(_volume >> 16));
		_lp.write(0x01, static_cast<uint8_t>(_volume >> 8));
		_lp.write(0x00, static_cast<uint8_t>(_volume));
	}

	uint32_t readCurrentVolume(lpLib::LP& _lp, const uint8_t _voice)
	{
		_lp.write(0x10, _voice);
		uint32_t result = _lp.read(2) | (uint32_t(_lp.read(3)) << 8);
		_lp.write(0x12, _voice);
		result |= uint32_t(_lp.read(2)) << 16;
		result |= uint32_t(_lp.read(3) & 3) << 24;
		return result;
	}
}

int main()
{
	// These hashes cover every entry of the chip measured envelope curves. The
	// implementation obtains them from shift/mantissa logic, not stored data.
	require(curveHash(lpLib::LP::envelopeLevel) == 0x6a7dd43deb9e4f83ull,
		"envelope level generator differs from the measured LP curve");
	require(curveHash(lpLib::LP::envelopeIncrement) == 0xb79d957849071db3ull,
		"envelope rate generator differs from the measured LP curve");

	// LP reads the table in pcmInterpolation.h, which GP and XP share, so this guards all three,
	// including the two corrections from f61931aac.
	require(interpolationRomHash() == 0xfcc667b31638dea0ull,
		"interpolation ROM differs from the decoded LP contents");
	require(lpLib::LP::interpolationWeight(0, 10) == 3535, "corrected first-tap entry");
	require(lpLib::LP::interpolationWeight(1, 65) == 2071, "corrected second-tap entry");
	require(lpLib::LP::interpolationWeight(3, 0) == 0, "invalid interpolation ROM tap");

	std::vector<uint8_t> rom(0x400000, 0);
	lpLib::LP lp(rom);
	std::vector<bool> interruptLevels;
	lp.setInterruptCallback([&interruptLevels](const bool _level) { interruptLevels.push_back(_level); });
	programImmediateEnvelope(lp, 0);
	programImmediateEnvelope(lp, 1);
	lp.write(0x11, 0x03);
	lp.renderSample();
	require(lp.interruptActive(), "first completion must assert the interrupt");
	require(lp.queuedInterrupts() == 1, "second simultaneous completion must be queued");
	require(lp.read(0) == 0, "first completed voice");
	require(lp.interruptActive(), "queued completion must keep the interrupt active");
	require(lp.queuedInterrupts() == 0, "reading must promote exactly one queued completion");
	require(lp.read(0) == 1, "second completed voice");
	require(!lp.interruptActive(), "final read must clear the interrupt");
	require(lp.queuedInterrupts() == 0, "final read must leave no phantom voice-zero completion");
	require(!interruptLevels.empty() && !interruptLevels.back(), "interrupt line must finish deasserted");

	// Direction follows the current and target levels; the rate byte controls
	// magnitude only. An armed zero-distance segment completes immediately.
	lp.reset();
	writeCurrentVolume(lp, 0, lpLib::LP::envelopeLevel(0x80));
	lp.write(0x07, 0x70);
	lp.write(0x06, 0x7f);
	lp.write(0x11, 0x01);
	lp.renderSample();
	require(readCurrentVolume(lp, 0) == static_cast<uint32_t>(lpLib::LP::envelopeLevel(0x70)),
		"envelope direction must follow a lower target");
	require(lp.interruptActive(), "completed downward envelope must interrupt");
	require(lp.read(0) == 0, "downward envelope completion voice");
	lp.write(0x07, 0x70);
	lp.write(0x06, 0x81);
	lp.renderSample();
	require(lp.interruptActive(), "armed zero-distance envelope must interrupt");
	require(lp.read(0) == 0, "zero-distance envelope completion voice");

	// Rate FF is the firmware's immediate-load command for any target.
	writeCurrentVolume(lp, 0, 0);
	lp.write(0x07, 0xf0);
	lp.write(0x06, 0xff);
	lp.renderSample();
	require(readCurrentVolume(lp, 0) == static_cast<uint32_t>(lpLib::LP::envelopeLevel(0xf0)),
		"FF/F0 immediate envelope preset");
	require(!lp.interruptActive(), "immediate envelope must not interrupt");

	// Loop contents never select another decoder. Short and long loops both run
	// through the LP-2's one continuous signed 12-bit per-voice DPCM RAM.
	lp.reset();
	for (unsigned i = 0; i < 5; ++i) rom[i] = 0x70; // decoded delta +1024
	lp.write(0x1f, 0);
	lp.write(0x03, 0x00); lp.write(0x02, 0x00);
	lp.write(0x05, 0x40); lp.write(0x04, 0x00); // one ROM byte per output sample
	lp.write(0x0b, 0x00); lp.write(0x0a, 0x00);
	lp.write(0x09, 0x00); lp.write(0x08, 0x00);
	lp.write(0x0d, 0x00); lp.write(0x0c, 0x01); // end byte 4
	lp.write(0x0f, 0x00); lp.write(0x0e, 0x00);
	require(lp.lp1RamA()[2] == 0x4000, "LP-1 RAM A must hold the host step word");
	require(lp.lp1RamB()[2] == 0x0001 && lp.lp1RamB()[3] == 0,
		"LP-1 RAM B must hold the host end and loop words");
	lp.write(0x11, 0x01);
	lp.renderSample();
	require(lp.voiceState(0).predictor == 1024, "shared predictor first addition");
	require(lp.lp2Ram()[0] == 0x0400, "LP-2 RAM must hold the 12-bit voice predictor");
	lp.renderSample();
	require(lp.voiceState(0).predictor == -2048, "DPCM predictor must wrap at signed 12 bits");
	require(lp.lp2Ram()[0] == 0x0800, "LP-2 RAM stores predictor bits without widening");
	lp.renderSample();
	lp.renderSample();
	require(lp.voiceState(0).predictor == 0, "short loop must use the same wrapping predictor");
	lp.renderSample();
	require(lp.voiceState(0).predictor == 1024, "looping must not restore a synthetic predictor anchor");
	lp.renderSample();
	require(lp.voiceState(0).predictor == -2048, "looping must continue through the same accumulator");

	// The loop byte is the predictor anchor and is consumed only on the initial
	// pass. Alternate playback reflects between loop+1 and end. The encoded
	// repeating section below sums to zero and therefore returns to exactly the
	// same predictor after every complete round trip.
	lp.reset();
	rom[0] = 5;
	rom[1] = 1;
	rom[2] = 2;
	rom[3] = 3;
	rom[4] = static_cast<uint8_t>(-6);
	lp.write(0x1f, 0);
	lp.write(0x03, 0x80); lp.write(0x02, 0x00); // alternate loop, immediate full volume
	lp.write(0x05, 0x40); lp.write(0x04, 0x00); // one ROM byte per output sample
	lp.write(0x0b, 0x00); lp.write(0x0a, 0x00);
	lp.write(0x09, 0x00); lp.write(0x08, 0x00);
	lp.write(0x0d, 0x00); lp.write(0x0c, 0x01); // end byte 4
	lp.write(0x0f, 0x00); lp.write(0x0e, 0x00); // loop byte 0
	lp.write(0x11, 0x01);
	constexpr std::array<int32_t, 17> alternatePredictors{{5, 6, 8, 11, 5, -1, 2, 4, 5,
		6, 8, 11, 5, -1, 2, 4, 5}};
	for (const int32_t expected : alternatePredictors)
	{
		lp.renderSample();
		require(lp.voiceState(0).predictor == expected,
			"alternate-loop predictor must repeat without endpoint drift");
	}
	require(lp.voiceState(0).address == 0x4000,
		"alternate-loop round trip must restore its address phase");

	// A long loop follows exactly the same accumulator path.
	lp.reset();
	std::fill(rom.begin(), rom.begin() + 520, 0x70); // decoded delta +1024
	lp.write(0x1f, 0);
	lp.write(0x03, 0x00); lp.write(0x02, 0x00);
	lp.write(0x05, 0x40); lp.write(0x04, 0x00);
	lp.write(0x0b, 0x00); lp.write(0x0a, 0x00);
	lp.write(0x09, 0x00); lp.write(0x08, 0x00);
	lp.write(0x0d, 0x00); lp.write(0x0c, 129); // end byte 516
	lp.write(0x0f, 0x00); lp.write(0x0e, 0x00);
	lp.write(0x11, 0x01);
	lp.renderSample();
	lp.renderSample();
	lp.renderSample();
	require(lp.voiceState(0).predictor == -1024,
		"ordinary PCM uses the same signed 12-bit predictor");
	for (unsigned sample = 3; sample < 517; ++sample)
		lp.renderSample();
	require(lp.voiceState(0).predictor == 1024,
		"ordinary PCM loop must continue through the signed 12-bit accumulator");

	// A captured program (program18 below) configures two ordinary alternate-loop
	// voices over this five-byte waveform. This is intentionally expressed only in
	// host-visible LP registers: there is no loop modulation selector or alternate
	// decoder involved.
	std::vector<uint8_t> program18Rom(0x100000, 0);
	constexpr uint32_t program18LoopAddress = 0x80000 + 0x16ef * 4;
	const uint8_t program18Loop[] = {0x33, 0x3a, 0x20, 0x24, 0xd8};
	std::copy(std::begin(program18Loop), std::end(program18Loop),
		program18Rom.begin() + program18LoopAddress);
	lpLib::LP program18Lp(program18Rom);
	auto programVoice = [&program18Lp](const uint8_t _voice, const uint16_t _step)
	{
		program18Lp.write(0x1f, _voice);
		program18Lp.write(0x01, 0x00); program18Lp.write(0x00, 0x00);
		program18Lp.write(0x03, 0x8a); program18Lp.write(0x02, 0x00); // mode 88, volume 02000000
		program18Lp.write(0x05, static_cast<uint8_t>(_step >> 8));
		program18Lp.write(0x04, static_cast<uint8_t>(_step));
		program18Lp.write(0x07, 0xf0); program18Lp.write(0x06, 0x00);
		program18Lp.write(0x09, 0x00); program18Lp.write(0x08, 0x00);
		program18Lp.write(0x0b, 0x16); program18Lp.write(0x0a, 0xef);
		program18Lp.write(0x0d, 0x16); program18Lp.write(0x0c, 0xf0);
		program18Lp.write(0x0f, 0x16); program18Lp.write(0x0e, 0xef);
	};
	programVoice(0, 0x218d);
	programVoice(1, 0x216f);
	program18Lp.write(0x11, 0x03);
	uint64_t program18Hash = 1469598103934665603ull;
	std::array<int32_t, 2> program18Peaks{};
	for (unsigned sampleIndex = 0; sampleIndex < 8192; ++sampleIndex)
	{
		const auto voices = program18Lp.renderSample();
		for (unsigned voice = 0; voice < 2; ++voice)
		{
			program18Peaks[voice] = std::max(program18Peaks[voice], std::abs(voices[voice]));
			program18Hash ^= static_cast<uint32_t>(voices[voice]);
			program18Hash *= 1099511628211ull;
		}
	}
	require(program18Lp.activeVoiceCount() == 2, "program 18 must retain both configured voices");
	require(program18Peaks[0] == 70816 && program18Peaks[1] == 70816,
		"program 18 voices must remain audibly nonzero at their observed levels");
	require(program18Hash == 0x275905e29f6cc683ull,
		"program 18 unified-loop waveform changed");
	require(program18Lp.voiceState(0).predictor == -1892 && program18Lp.voiceState(1).predictor == 1732,
		"program 18 must retain signed 12-bit predictor wrapping");
	require(program18Lp.voiceState(0).address == 0x16efe000
		&& program18Lp.voiceState(1).address == 0x16efa000,
		"program 18 alternate-loop phase progression changed");

	std::puts("lp tests passed");
	return 0;
}
