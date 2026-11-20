// assert() is this test's only checker; keep it armed in Release builds,
// where CMake's -DNDEBUG would otherwise compile every assertion out.
#undef NDEBUG

#include "mt32reverb.h"
#include "mt32reverb_interpreter.h"
#include "mt32reverb_jit.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

// The JIT backends against the interpreter: every program of a ROM, every saw selector,
// from a randomised state, for a run of random full-scale input. Random ROM bytes exercise
// every control-bit combination, which a real microcode image does not; a real image can be
// passed as the first argument and is checked the same way.

namespace
{
	using namespace mt32ReverbLib;

	std::vector<uint8_t> randomRom(const size_t _size, const uint32_t _seed)
	{
		std::mt19937 gen(_seed);
		std::vector<uint8_t> rom(_size);
		for (auto& b : rom)
			b = static_cast<uint8_t>(gen());
		return rom;
	}

	int16_t randomSample(std::mt19937& _gen)
	{
		return static_cast<int16_t>(_gen());
	}

	void randomiseState(State& _s, std::mt19937& _gen)
	{
		for (auto& w : _s.ram)
			w = randomSample(_gen);
		_s.position = static_cast<int32_t>(_gen() & RamMask);
		_s.accumulator = randomSample(_gen);
		_s.shifter = randomSample(_gen);
		_s.carry = static_cast<int32_t>(_gen() & 1);
	}

	bool sameState(const State& _a, const State& _b)
	{
		return _a.ram == _b.ram && _a.position == _b.position && _a.accumulator == _b.accumulator &&
		       _a.shifter == _b.shifter && _a.carry == _b.carry && _a.outLeft == _b.outLeft &&
		       _a.outRight == _b.outRight;
	}

	constexpr uint8_t Nop = 0x16;

	struct Program
	{
		std::vector<uint8_t> bytes = std::vector<uint8_t>(ProgramBytes, 0);
		Program() { for (unsigned k = 0; k < StepCount; ++k) set(k, Nop, Nop); }
		// _mask0 / _mask1: the saw-mask bit that lives in byte 2 (bit 0 of the half's mask).
		void set(const unsigned _step, const uint8_t _c0, const uint8_t _c1, const bool _mask0 = false, const bool _mask1 = false)
		{
			bytes[_step * 4 + 1] = _c0;
			bytes[_step * 4 + 2] = static_cast<uint8_t>((_mask0 ? 0x80 : 0) | (_mask1 ? 0x40 : 0));
			bytes[_step * 4 + 3] = _c1;
		}
	};

	// One frame on the interpreter and, where there is one, on the JIT; both must agree.
	State runRule(const Program& _p, State _s)
	{
		State interpreted = _s;
		Interpreter::renderFrame(_p.bytes.data(), interpreted);
#if MT32REVERB_JIT
		Jit jit;
		assert(jit.compile(_p.bytes.data(), _p.bytes.size()));
		jit.program(0)(&_s);
		assert(sameState(interpreted, _s));
#endif
		return interpreted;
	}

	void testHalfStepRules()
	{
		// A clear leaves the accumulator at zero even when the saw selector is in the mask:
		// it does not start a new sum from the shifter.
		{
			Program p;
			p.set(0, 0xe6, Nop, true);
			State s;
			s.accumulator = 1234;
			s.shifter = 100;
			s.sawBits = 1;
			const auto r = runRule(p, s);
			assert(r.accumulator == 0);
			assert(r.outRight == 0 && r.outLeft == 0);
		}
		// The add of a half-step that loads uses the carry from before the load.
		{
			Program p;
			p.set(0, 0x32, Nop);	// add (mask bit 1), then load
			State s;
			s.shifter = 10;
			s.carry = 1;
			s.sawBits = 2;
			assert(runRule(p, s).accumulator == 11);
		}
		// A load keeps the old shifter's low bit as the carry while the loaded word is
		// negative, and drops it for a positive one.
		{
			Program p;
			p.set(0, 0x12, 0x36);	// load, then add
			State s;
			s.shifter = 3;
			s.sawBits = 2;
			s.ram[0] = -4;
			assert(runRule(p, s).accumulator == -3);
			s.ram[0] = 4;
			assert(runRule(p, s).accumulator == 4);
		}
		// The inversion belongs to the add: a half-step whose mask misses leaves the
		// accumulator as it is, one that hits stores the inverted sum.
		{
			Program p;
			p.set(0, 0x3e, Nop);	// negate, mask bit 1
			State s;
			s.accumulator = 5;
			s.sawBits = 1;
			assert(runRule(p, s).accumulator == 5);
			s.sawBits = 2;
			assert(runRule(p, s).accumulator == ~5);
		}
		// The second half-step loads on its own control byte too.
		{
			Program p;
			p.set(0, Nop, 0x12);	// load on the second half
			p.set(1, 0x36, Nop);	// add
			State s;
			s.sawBits = 2;
			s.ram[0] = 77;
			assert(runRule(p, s).accumulator == 77);
		}
	}

#if MT32REVERB_JIT
	// Both engines from one state, frame by frame, on every program and saw selector.
	void compareEngines(const std::vector<uint8_t>& _rom, const unsigned _frames, const uint32_t _seed)
	{
		Jit jit;
		assert(jit.compile(_rom.data(), _rom.size()));
		assert(jit.programCount() == _rom.size() / ProgramBytes);

		std::mt19937 gen(_seed);
		for (size_t p = 0; p < jit.programCount(); ++p)
		{
			const uint8_t* program = _rom.data() + p * ProgramBytes;
			const auto run = jit.program(p);
			assert(run);
			for (unsigned saw = 0; saw < 4; ++saw)
			{
				State a;
				randomiseState(a, gen);
				a.sawBits = 1 << saw;
				State b = a;
				for (unsigned f = 0; f < _frames; ++f)
				{
					a.inputLeft = b.inputLeft = randomSample(gen);
					a.inputRight = b.inputRight = randomSample(gen);
					Interpreter::renderFrame(program, a);
					run(&b);
					assert(a.outLeft == b.outLeft);
					assert(a.outRight == b.outRight);
				}
				assert(sameState(a, b));
			}
		}
	}

	// The engine switch is invisible: a JIT-driven chip and an interpreter-driven one produce
	// the same output for the same parameter writes.
	void compareChip(const std::vector<uint8_t>& _rom, const uint32_t _seed)
	{
		Mt32Reverb chip(_rom);
		assert(chip.isValid());
		assert(chip.jitActive());

		State ref;
		std::mt19937 gen(_seed);
		for (unsigned mode = 0; mode < 8; ++mode)
		{
			for (unsigned time = 0; time < 8; ++time)
			{
				for (unsigned level = 0; level < 8; level += 3)
				{
					chip.setParameters(mode, time, level);
					assert(chip.jitActive());

					const unsigned m = mode & (_rom.size() == 0x8000 ? 7 : 3);
					const unsigned base = (m << 12) | ((level & 7) << 9) | ((time & 4) << 6);
					ref.sawBits = 1 << (time & 3);
					for (unsigned f = 0; f < 50; ++f)
					{
						const int32_t left = static_cast<int32_t>(gen() % 0x14000) - 0xa000;	// past full scale, too
						const int32_t right = static_cast<int32_t>(gen() % 0x14000) - 0xa000;
						ref.inputLeft = static_cast<int16_t>(std::clamp(left, -32768, 32767) >> 1);
						ref.inputRight = static_cast<int16_t>(std::clamp(right, -32768, 32767) >> 1);
						Interpreter::renderFrame(_rom.data() + base, ref);
						const auto out = chip.renderFrame({left, right});
						assert(out.first == ref.outLeft);
						assert(out.second == ref.outRight);
					}
					assert(sameState(chip.state(), ref));
				}
			}
		}
	}

	void benchmark(const std::vector<uint8_t>& _rom)
	{
		Jit jit;
		assert(jit.compile(_rom.data(), _rom.size()));
		constexpr unsigned frames = 200000;
		std::mt19937 gen(7);
		State a, b;
		a.sawBits = b.sawBits = 2;
		const auto run = jit.program(5);

		auto t0 = std::chrono::steady_clock::now();
		for (unsigned f = 0; f < frames; ++f)
		{
			a.inputLeft = randomSample(gen);
			a.inputRight = randomSample(gen);
			Interpreter::renderFrame(_rom.data() + 5 * ProgramBytes, a);
		}
		auto t1 = std::chrono::steady_clock::now();
		for (unsigned f = 0; f < frames; ++f)
		{
			b.inputLeft = randomSample(gen);
			b.inputRight = randomSample(gen);
			run(&b);
		}
		auto t2 = std::chrono::steady_clock::now();
		const auto us = [](const auto _d) { return std::chrono::duration_cast<std::chrono::microseconds>(_d).count(); };
		std::printf("%u frames: interpreter %lld us, jit %lld us\n", frames, static_cast<long long>(us(t1 - t0)),
		            static_cast<long long>(us(t2 - t1)));
	}
#endif
}

int main(const int _argc, char** _argv)
{
	using namespace mt32ReverbLib;

	// The chip rejects anything but a 16 or 32 KiB image and is silent without one.
	{
		Mt32Reverb chip;
		assert(!chip.isValid());
		assert(!chip.setRom(std::vector<uint8_t>(0x1000)));
		assert((chip.renderFrame({1000, -1000}) == std::pair<int32_t, int32_t>{}));
	}

	testHalfStepRules();

	std::vector<std::vector<uint8_t>> roms;
	roms.push_back(randomRom(0x8000, 1));
	roms.push_back(randomRom(0x8000, 2));
	roms.push_back(randomRom(0x4000, 3));
	if (_argc > 1)
	{
		if (FILE* f = std::fopen(_argv[1], "rb"))
		{
			std::vector<uint8_t> rom(0x8000);
			rom.resize(std::fread(rom.data(), 1, rom.size(), f));
			std::fclose(f);
			if (rom.size() == 0x4000 || rom.size() == 0x8000)
				roms.push_back(std::move(rom));
			else
				std::printf("ignoring %s: not a reverb ROM image\n", _argv[1]);
		}
	}

#if MT32REVERB_JIT
	uint32_t seed = 100;
	for (const auto& rom : roms)
	{
		compareEngines(rom, 300, seed++);
		compareChip(rom, seed++);
	}
	benchmark(roms.front());
	std::printf("mt32reverb: jit matches the interpreter on %zu ROM images\n", roms.size());
#else
	std::printf("mt32reverb: no jit backend on this host, interpreter only\n");
#endif
	return 0;
}
