#include "custom_chips/esp/esp_jit_backend.h"

#if CHIPS_JIT_ASMJIT
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
using E = esp::Interpreter<12>;
// Exercise integration, not just direct code generation: settling, cache keys,
// live host operands, inadmissible frames, reset, and mode switching.
static unsigned dispatcherChecks()
{
	auto d = std::make_unique<esp::Dispatcher<12>>();
	d->setEngine(esp::Engine::Jit);
	d->setBackgroundCompile(false);
	unsigned failures = 0;
	auto check = [&](bool condition, const char *what)
	{
		if (!condition)
		{
			std::printf("dispatcher: %s\n", what);
			++failures;
		}
	};
	auto configure = [&]()
	{
		d->setExternalClockPresent(true);
		const uint8_t config[] = {0xaa, 0, 0x90, 0x54, 0xc7, 0, 0, 0};
		for (unsigned i = 0; i < 8; ++i)
			d->hostWrite(0x2000 + i, config[i]);
	};
	auto frame = [&]()
	{
		d->runProgram(48);
		d->sync();
	};
	configure();
	for (unsigned i = 0; i < 64; ++i)
		frame();
	check(d->statistics().native == 0, "settling must remain lockstep even with synchronous compilation");
	frame();
	check(d->statistics().native == 1 && d->kernelReady(), "first settled frame must compile");
	auto compiles = d->statistics().compiles;
	d->hostWrite(0x2003, 0x55); // Host interface mode only: no generated-code dependency.
	frame();
	check(d->statistics().compiles == compiles, "host mode must not trigger recompilation");
	d->hostWrite(0x2003, 0x44); // Disable conditional branch decoding.
	check(!d->kernelReady(), "configuration edit must invalidate ready status");
	frame();
	check(d->statistics().compiles == ++compiles, "branch mode must recompile");
	d->hostWrite(0x2003, 0x14); // Raw single-word ERAM mode.
	frame();
	check(d->statistics().compiles == ++compiles, "ERAM mode must recompile");
	d->writePMem32(1024 + 700, 0x04000000); // Outside this frame's reachable instructions.
	check(!d->kernelReady(), "first off-path ERAM command must invalidate no-command specialization");
	auto native = d->statistics().native;
	frame();
	check(d->statistics().native == native, "program edits must restart settling");
	for (unsigned i = 0; i < 64; ++i)
		frame();
	check(d->statistics().compiles == ++compiles, "edited program must eventually compile");
	d->writePMem32(1024 + 700, 0);
	check(!d->kernelReady(), "removing the last ERAM command must invalidate specialization");
	for (unsigned i = 0; i < 65; ++i)
		frame();
	d->hostWrite(0x2005, 0x60); // Output inhibit disallows batched entry.
	native = d->statistics().native;
	frame();
	check(d->statistics().native == native, "inadmissible state must use interpreter");
	d->reset();
	configure();
	native = d->statistics().native;
	for (unsigned i = 0; i < 64; ++i)
		frame();
	check(d->statistics().native == native, "reset must restart settling");
	frame();
	check(d->statistics().native == native + 1, "reset must recover native execution");
	d->setEngine(esp::Engine::Interpreter);
	frame();
	d->setEngine(esp::Engine::Jit);
	native = d->statistics().native;
	frame();
	check(d->statistics().native == native + 1, "engine switching must preserve native operation");
	std::printf("dispatcher failures=%u\n", failures);
	return failures;
}

// Compare architectural values rather than object bytes/padding. In particular,
// this catches switching from lockstep to sequential execution at a worker-
// dependent frame instead of at the fixed settling boundary.
static std::vector<int64_t> frameState(const esp::Dispatcher<12>& d)
{
	const auto& r = d.runtime();
	std::vector<int64_t> v;
	auto add = [&](auto value) { v.push_back(int64_t(value)); };
	for (const auto& c : r.core)
	{
		for (auto value : c.iram) add(value);
		add(c.iramPos); add(c.pc); add(c.pcjumpat); add(c.pcjumpto);
		for (const auto* a : {&c.accA, &c.accB})
		{
			add(a->acc); add(a->head); add(a->outputClear);
			for (auto value : a->hist) add(value);
		}
		add(c.lastMulInputA); add(c.lastCoefficientTransform); add(c.lastMul30);
		add(c.skipfieldPos); add(c.skipfieldNeg); add(c.predicateReady); add(c.predicateValues);
		add(c.comparePending); add(c.compareNegative); add(c.dintPin); add(c.irq1Sign);
	}
	for (auto value : r.gram) add(value);
	for (auto value : r.mulcoeffs) add(value);
	for (auto value : r.mailbox) add(value);
	add(r.irq1History); add(r.irq1Request); add(r.publishedDint); add(r.frameCycle); add(r.framePaused);
	const auto& e = r.eram;
	add(e.readLatch); add(e.writeLatch); add(e.varOffset); add(e.readPacked); add(e.readPackedNext);
	add(e.writePackedNext); add(e.pos); add(e.effectiveAddr); add(e.immOffsetAccNext); add(e.highOffset);
	add(e.varOffsetNext); add(e.writeLatchNext); add(e.cycle); add(e.cycleCommit); add(e.cycleStartNext);
	add(e.cycleStartCurrent); add(e.nextCommandCycle); add(e.modeCurrent); add(e.modeNext);
	add(e.activeCurrent); add(e.activeNext); add(e.shortCurrent); add(e.shortNext); add(e.lowEffectiveAddr);
	add(e.readRawNext); add(e.deferredCommand); add(e.memoryMode); add(e.compress); add(e.staticBus);
	for (unsigned i = 0; i < e.size; ++i) { add(e.memory[i]); add(e.packed[i]); }
	return v;
}

// A pathological CFG must fail once, preserve the scheduling contract, and
// become compilable again after a structural edit. No allocation storm.
static unsigned compileLimitChecks()
{
	auto reference = std::make_unique<esp::Dispatcher<12>>();
	auto candidate = std::make_unique<esp::Dispatcher<12>>();
	reference->setEngine(esp::Engine::Interpreter);
	candidate->setBackgroundCompile(false);
	for (auto* d : {reference.get(), candidate.get()})
	{
		d->setExternalClockPresent(true);
		d->hostWrite(0x2003, 0x54);
	}
	std::mt19937 rng(123);
	for (unsigned pc = 0; pc < 768; ++pc)
	{
		uint32_t word = (0x50 << 16) | (0x42 << 10) | 127;
		if (pc % 4 == 2) word = (0x34 << 16) | ((0xc0 + rng() % 3) << 10) | (rng() % 768);
		for (auto* d : {reference.get(), candidate.get()}) d->writePMem32(pc, word);
	}
	unsigned failures = 0;
	for (unsigned frame = 0; frame < 67; ++frame)
	{
		if (frame >= 64) reference->interpreter().runSequential(esp::FRAME_CLOCKS);
		else reference->runProgram();
		reference->sync();
		candidate->runProgram();
		candidate->sync();
		if (frameState(*reference) != frameState(*candidate)) ++failures;
	}
	const auto& stats = candidate->statistics();
	if (stats.compiles != 1 || stats.failed != 1 || stats.native != 0) ++failures;
	for (unsigned pc = 0; pc < 768; ++pc) candidate->writePMem32(pc, 0);
	for (unsigned frame = 0; frame < 65; ++frame) { candidate->runProgram(); candidate->sync(); }
	if (stats.compiles != 2 || stats.failed != 1 || stats.native != 1) ++failures;
	std::printf("compile limit failures=%u\n", failures);
	return failures;
}

static unsigned backgroundChecks()
{
	auto reference = std::make_unique<esp::Dispatcher<12>>();
	auto candidate = std::make_unique<esp::Dispatcher<12>>();
	reference->setBackgroundCompile(false);
	for (auto* d : {reference.get(), candidate.get()}) d->setEngine(esp::Engine::Jit);
	unsigned failures = 0;
	auto configure = [](auto& d)
	{
		d.setExternalClockPresent(true);
		d.hostWrite(0x2003, 0x54);
		// The consumer reads before the producer's write in lockstep. This
		// deliberately distinguishes the pending-compile scheduling policy.
		d.writePMem32(0x400, (4u << 16) | (0x10 << 10) | 0x7f);
		d.writePMem32(0x408, (0x28u << 16) | (0x20 << 10));
		d.writePMem32(0, (0x24u << 16) | (0x20 << 10) | 0x7f);
		d.writePMem32(4, (8u << 16) | (0x30 << 10));
	};
	for (auto* d : {reference.get(), candidate.get()}) configure(*d);
	for (unsigned frame = 0; frame < 330; ++frame)
	{
		for (auto* d : {reference.get(), candidate.get()})
		{
			if (frame == 65 || frame == 66) d->writePMem32(4, (8u << 16) | ((frame - 16) << 10));
			if (frame == 131) d->hostWrite(0x2003, 0x14);
			if (frame == 133) d->writePMem32(0x400, (4u << 16) | (0x10 << 10) | 0x41);
			if (frame == 170) d->setEngine(esp::Engine::Interpreter);
			if (frame == 190) d->setEngine(esp::Engine::Jit);
			if (frame == 220) { d->reset(); configure(*d); }
			if (d == candidate.get() && frame == 200) d->setBackgroundCompile(false);
			if (d == candidate.get() && frame == 210) d->setBackgroundCompile(true);
			d->writeIRAM1(int32_t(frame * 7919) - 0x100000, 0x10);
			d->runProgram(frame == 132 ? 51 : 48);
			d->sync();
		}
		if (frameState(*reference) != frameState(*candidate))
		{
			std::printf("background frame %u differs\n", frame);
			++failures;
			break;
		}
		if (frame == 64 && candidate->statistics().native != 0)
		{
			std::puts("background request compiled synchronously");
			++failures;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	if (!candidate->kernelReady() || !candidate->statistics().native || candidate->statistics().failed)
	{
		std::puts("background dispatcher did not recover native execution");
		++failures;
	}
	// Destruction must join a compile that was just requested, including
	// when the owner switches policy before taking the completed result.
	for (unsigned i = 0; i < 4; ++i)
	{
		auto d = std::make_unique<esp::Dispatcher<12>>();
		d->setEngine(esp::Engine::Jit);
		configure(*d);
		for (unsigned j = 0; j < 65; ++j) { d->runProgram(768); d->sync(); }
		if (i & 1) d->setEngine(esp::Engine::Interpreter);
	}
	std::printf("background dispatcher failures=%u\n", failures);
	return failures;
}

static unsigned saturationChecks()
{
	unsigned failures = 0;
	for (int32_t value : {-0x20000000, -0x800001, -0x800000, -0x7fffff, -1, 0, 1,
		0x7ffffe, 0x7fffff, 0x800000, 0x1fffffff})
	{
		auto d = std::make_unique<esp::Dispatcher<12>>();
		d->setExternalClockPresent(true);
		for (unsigned c = 0; c < 2; ++c)
		{
			auto& a = d->runtime().core[c].accA;
			a.acc = value;
			for (auto& h : a.hist) h = value;
			for (unsigned pc = 0; pc < 2; ++pc)
				d->writePMem32(c * 1024 + pc, (8u << 16) | ((0x40 + pc) << 10));
		}
		asmjit::JitRuntime runtime;
		auto snapshot = esp::ProgramSnapshot::take(d->interpreter());
		auto kernel = esp::JitBackend<12>::compile(runtime, snapshot, 3);
		if (!kernel) { ++failures; continue; }
		esp::KernelFrame frame;
		frame.image = esp::makeJitFrame(d->interpreter());
		kernel(&frame);
		const int32_t expected = std::max(-0x800000, std::min(0x7fffff, value));
		for (unsigned c = 0; c < 2; ++c)
			for (unsigned pc = 0; pc < 2; ++pc)
				if (d->runtime().core[c].readIram(0x40 + pc) != expected) ++failures;
		runtime.release(kernel);
	}
	std::printf("saturation boundary failures=%u\n", failures);
	return failures;
}

// Keeping the raw multiplicand for DMAC must not alias the register that the
// x86 emitter truncates and reuses for the product. Include noncanonical upper
// bits, negative byte coefficients and stores that consume older pipeline values.
static unsigned operandLifetimeChecks()
{
	unsigned failures = 0;
	for (int32_t value : {INT32_MIN, INT32_MAX, 0x01800001, -0x01800001, 0x7fffff, -0x800000})
		for (unsigned clocks : {1u, 2u, 3u, 7u})
		{
			auto reference = std::make_unique<esp::Dispatcher<12>>();
			auto candidate = std::make_unique<esp::Dispatcher<12>>();
			const uint8_t configuration[] = {0xaa, 0, 0x90, 0x54, 0xc7, 0, 0, 0};
			for (auto* d : {reference.get(), candidate.get()})
			{
				d->setExternalClockPresent(true);
				for (unsigned i = 0; i < 8; ++i)
					d->hostWrite(0x2000 + i, configuration[i]);
				for (unsigned c = 0; c < 2; ++c)
				{
					d->runtime().core[c].writeIram(value, 0x40);
					for (unsigned pc = 0; pc < 8; ++pc)
					{
						uint32_t word = (0x40u << 10) | (1u << 8) | 64;
						if (pc % 3 == 1) word = (0x34u << 16) | (0xc6u << 10) | 0x80;
						if (pc % 3 == 2) word = (8u << 16) | (0x42u << 10) | 0x7f;
						d->writePMem32(c * 1024 + pc, word);
					}
				}
			}
			asmjit::JitRuntime runtime;
			const auto snapshot = esp::ProgramSnapshot::take(candidate->interpreter());
			auto kernel = esp::JitBackend<12>::compile(runtime, snapshot, clocks);
			if (!kernel) { ++failures; continue; }
			esp::KernelFrame frame;
			frame.image = esp::makeJitFrame(candidate->interpreter());
			reference->interpreter().runSequential(clocks);
			kernel(&frame);
			candidate->interpreter().finishBatchedFrame(clocks);
			if (frameState(*reference) != frameState(*candidate)) ++failures;
			runtime.release(kernel);
		}
	std::printf("operand lifetime failures=%u\n", failures);
	return failures;
}

int main()
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	unsigned failures = dispatcherChecks() + backgroundChecks() + saturationChecks() + compileLimitChecks() + operandLifetimeChecks(), checks = 0;
	std::mt19937 rng(123);
	for (bool staticBus : {false, true})
		for (bool branches : {false, true})
			for (bool normalized : {false, true})
				for (unsigned mode : {0x54u, 0x34u, 0x14u, 0x74u})
					for (unsigned kind = 0; kind < 23; ++kind)
						for (unsigned clocks : {1u, 2u, 3u, 7u, 48u, 384u, 754u, 755u, 756u, 757u, 758u, 768u})
						{
							if ((kind == 15 || kind == 22) && clocks > 48)
								continue;
							auto a = std::make_unique<E>(), b = std::make_unique<E>();
							const uint8_t cfg[] = {0xaa, 0,
												   0x90, uint8_t(branches ? mode : (mode & ~0x10u)),
												   0xc7, uint8_t(staticBus ? 0x10 : 0),
												   0,	 0};
							for (auto *e : {a.get(), b.get()})
							{
								e->setExternalClockPresent(true);
								for (unsigned i = 0; i < 8; ++i)
									e->hostWrite(0x2000 + i, cfg[i]);
							}
							for (unsigned c = 0; c < 2; ++c)
							{
								for (unsigned pc = 0; pc < 768; ++pc)
								{
									uint32_t op = (rng() % 4) * 4;
									if (op >= 8)
										op += 8;
									if (kind == 1 && pc % 9 == 5)
										op = (rng() % 4) * 4 + 0x08;
									if (kind == 2 && pc % 9 == 5)
										op = 0x20;
									if (kind == 3 && pc % 9 == 5)
										op = 0x50;
									uint32_t word =
										(op << 16) | ((0x40 + (rng() % 16)) << 10) | (1 << 8) | (rng() % 256);
									if (kind == 4 && pc == 3)
										word = (0x34 << 16) | (0xc3 << 10) | 1;
									if (kind == 5 && pc == 3)
										word = (0x34 << 16) | (0xc3 << 10) | 900;
									if (kind == 9 && pc % 15 == 7)
										word = (0x34 << 16) | ((0xc0 + rng() % 4) << 10) | (pc - 3);
									if (kind == 10 && pc % 11 == 2)
										word = (0x30 << 16) | (0x42 << 10) | ((rng() % 8) << 5) | ((rng() % 8) << 2);
									if (kind == 10 && pc % 11 == 3)
										word = (0x34 << 16) | (0xc6 << 10) | 64;
									if (kind == 10 && pc % 11 == 7)
										word = (0x34 << 16) | ((0xa0 + rng() % 6) << 10) | 64;
									if (kind >= 6 && kind <= 8 && c == 1)
										word |= ((pc % (kind == 7	? 5
														: kind == 8 ? 6
																	: 16) ==
														  0
													  ? (8u * (1 + rng() % 3))
												  : pc % (kind == 7	  ? 5
														  : kind == 8 ? 6
																	  : 16) <
														  5
													  ? unsigned(rng() % 32)
													  : 0u)
												 << 23);
									if ((kind == 21 || kind == 22) && c == 1)
									{
										// Read each intermediate compressed phase, including short-gap holds.
										// A backward edge must keep every latch value its loop can observe.
										const unsigned slot = pc % 12;
										if (slot >= 5 && slot <= 7)
											word = (0x34 << 16) | ((0xcc + slot % 4) << 10) | 127;
										if (slot == 8)
											word = (0x34 << 16) | (0xcb << 10) | 127;
										if (kind == 22 && slot == 9)
											word = (0x34 << 16) | ((0xc0 + rng() % 3) << 10) | (pc - slot + 5);
										word |= (slot == 0	 ? ((pc / 12) % 2 ? 16u : 8u)
												 : slot <= 4 ? rng() % 32
															 : 0u)
												<< 23;
									}
									if (kind == 20 && c == 1)
									{
										// Change the write sample during and between ERAM phases. A packed
										// value may be reused only up to the next DSP write-latch instruction.
										const unsigned slot = pc % 16;
										if (slot == 5 || slot == 6 || slot == 10)
											word = (0x34 << 16) | (0xcb << 10) | 127;
										word |= (slot == 0 ? 16u : slot <= 4 ? rng() % 32 : 0u) << 23;
									}
									if (kind == 18 || kind == 19)
									{
										// Built-in constants, including instructions that instead read GRAM
										// or replace the operand. CMP must prevent unconditional folding.
										const unsigned code = kind == 18 ? (pc % 32) * 4 : pc % 12 == 0 ? 0x50 : 0;
										word =
											(code << 16) | ((1 + (pc % 4)) << 10) | ((rng() % 4) << 8) | (rng() % 256);
									}
									if (kind == 17)
									{
										// All arithmetic/store forms: dead MAC elimination must retain stores,
										// and signed-24 facts must preserve folded and rectified inputs.
										word = ((pc % 32) * 4 << 16) | ((0x40 + rng() % 8) << 10) | ((rng() % 4) << 8) |
											   (rng() % 256);
										if (pc % 32 == 13)
											word = (0x34 << 16) | (0xc6 << 10) | (rng() % 256);
									}
									if (kind == 16)
									{
										// At PC 7, taken and fallthrough paths have different previous
										// multiply kinds/transforms. PC 0 also exercises unknown entry state.
										const unsigned slot = pc % 12;
										word = (0x40 << 10) | (1 << 8) | 64;
										if (slot == 0 || slot == 6 || slot == 7)
											word = (0x34 << 16) | (0xc6 << 10) | 64;
										if (slot == 4 || slot == 5)
											word = (0x30 << 16) | (0x42 << 10) | (5 << 5) | (slot == 4 ? 16 : 8);
										if (slot == 2)
											word = (0x34 << 16) | ((0xc0 + rng() % 3) << 10) | (pc - slot + 7);
										if (slot == 8)
											word = (0x34 << 16) | (0xc3 << 10) | (pc - slot);
									}
									if (kind == 15)
									{
										const unsigned slot = pc % 16;
										if (slot == 0)
											word = (0x50 << 16) | (0x42 << 10) | 127;
										if (slot == 6)
											word = (0x34 << 16) | ((0xc0 + rng() % 3) << 10) | (pc - slot);
										if (slot == 2 && c == 1)
											word = (0x34 << 16) | (0xc7 << 10) | 127;
										if (slot >= 3 && slot <= 5)
											word = (0x08 << 16) | (0x42 << 10) | 127;
										if (c == 1)
											word |= (slot == 0 ? 0x18u : slot <= 4 ? rng() % 32 : 0u) << 23;
									}

									if (kind == 13 && pc % 7 < 3)
										word = (0x34 << 16) | ((0xc0 + rng() % 4) << 10) | ((pc + 5) % 768);
									if (kind == 14)
									{
										if (pc % 9 == 1)
											word = (0x34 << 16) | ((0xc0 + rng() % 4) << 10) | ((pc + 3) % 768);
										else if (pc % 9 < 5)
											word = (0x08 << 16) | (0x42 << 10) | (rng() % 256);
									}
									if (kind == 11 && pc % 4 == 3)
										word = 0;
									if (kind == 12 && c == 1)
									{
										if (pc == 5)
											word = (0x34 << 16) | ((0xc0 + rng() % 3) << 10) | 30;
										if (pc >= 30)
											word |= ((pc % 16 == 0	? 8u
													  : pc % 16 < 5 ? unsigned(rng() % 32)
																	: 0u)
													 << 23);
									}

									a->writePMem32(c * 1024 + pc, word);
									b->writePMem32(c * 1024 + pc, word);
								}
								for (unsigned i = 0; i < 256; ++i)
								{
									int32_t v = esp::se<24>(rng());
									a->runtime().core[c].writeIram(v, i);
									b->runtime().core[c].writeIram(v, i);
								}
							}
							for (unsigned c = 0; c < 2; ++c)
							{
								auto &x = a->runtime().core[c];
								auto &y = b->runtime().core[c];
								x.accA.acc = y.accA.acc = esp::se<30>(rng());
								x.accB.acc = y.accB.acc = esp::se<30>(rng());
								for (unsigned j = 0; j < 3; ++j)
								{
									x.accA.hist[j] = y.accA.hist[j] = esp::se<30>(rng());
									x.accB.hist[j] = y.accB.hist[j] = esp::se<30>(rng());
								}
								x.lastMulInputA = y.lastMulInputA = esp::se<24>(rng());
								x.lastMul30 = y.lastMul30 = bool(rng() % 2);
								x.lastCoefficientTransform = y.lastCoefficientTransform = (rng() % 4) * 8;
							}
							for (unsigned j = 0; j < 8; ++j)
								a->runtime().mulcoeffs[j] = b->runtime().mulcoeffs[j] = esp::se<24>(rng());
							for (unsigned i = 0; i < 4096; ++i)
							{
								auto v = esp::se<24>(rng());
								a->runtime().eram.memory[i] = b->runtime().eram.memory[i] = v;
								a->runtime().eram.packed[i] = b->runtime().eram.packed[i] = esp::packEram(v);
							}
							a->runtime().eram.writeLatch = b->runtime().eram.writeLatch = -1234567;
							a->runtime().eram.varOffset = b->runtime().eram.varOffset = 0x654321;
							if (normalized)
								for (unsigned c = 0; c < 2; ++c)
									for (auto *e : {a.get(), b.get()})
									{
										e->runtime().core[c].accA.acc = e->runtime().core[c].accA.hist[2];
										e->runtime().core[c].accB.acc = e->runtime().core[c].accB.hist[2];
									}
							auto snap = esp::ProgramSnapshot::take(*a);
							// compile() decodes a copy, kinds 18/19 below read the instructions of this one
							snap.decode();
							asmjit::JitRuntime jit;
							auto fn = esp::JitBackend<12>::compile(jit, snap, clocks);
							if (!fn)
							{
								std::printf("compile failed: staticBus=%u branches=%u normalized=%u mode=%x kind=%u "
											"clocks=%u\n",
											staticBus, branches, normalized, mode, kind, clocks);
								return 2;
							}
							// Address operands are live: exercise the table after compilation.
							if (kind == 6 && normalized)
								for (unsigned pc = 0; pc < 768; ++pc)
								{
									const uint32_t word = snap.words[1][pc] ^ (7u << 23);
									a->writePMem32(1024 + pc, word);
									b->writePMem32(1024 + pc, word);
								}
							if ((kind == 18 || kind == 19) && normalized)
								for (unsigned c = 0; c < 2; ++c)
									for (unsigned pc = 0; pc < 768; ++pc)
										if (snap.instruction(c, pc).op == esp::Op::Mac)
										{
											// Folding the multiplicand must not freeze host-editable
											// coefficients/shifts.
											const uint32_t word = (snap.words[c][pc] & ~0x3ffu) | (rng() & 0x3ffu);
											a->writePMem32(c * 1024 + pc, word);
											b->writePMem32(c * 1024 + pc, word);
										}
							esp::KernelFrame f;
							f.image = esp::makeJitFrame(*b);
							a->runSequential(clocks);
							fn(&f);
							b->finishBatchedFrame(clocks);
							jit.release(fn);
							auto &x = a->runtime();
							auto &y = b->runtime();
							bool match = true;
							for (unsigned c = 0; c < 2; ++c)
							{
#define CHECK(v)                                                                                                       \
	if (x.core[c].v != y.core[c].v)                                                                                    \
	{                                                                                                                  \
		if (failures < 15)                                                                                             \
			std::printf("kind=%u clocks=%u core=%u %s %d != %d\n", kind, clocks, c, #v, int(x.core[c].v),              \
						int(y.core[c].v));                                                                             \
		match = false;                                                                                                 \
	}
								CHECK(pc);
								CHECK(pcjumpat);
								CHECK(pcjumpto);
								CHECK(accA.acc);
								CHECK(accB.acc);
								CHECK(accA.head);
								CHECK(accB.head);
								CHECK(lastMulInputA);
								CHECK(lastMul30);
								CHECK(lastCoefficientTransform);
								CHECK(skipfieldPos);
								CHECK(skipfieldNeg);
								CHECK(predicateReady);
								CHECK(predicateValues);
								CHECK(comparePending);
								CHECK(compareNegative);
								CHECK(dintPin);
								CHECK(irq1Sign);
								for (unsigned i = 0; i < 3; ++i)
								{
									CHECK(accA.hist[i]);
									CHECK(accB.hist[i]);
								}
								for (unsigned i = 0; i < 512; ++i)
								{
									CHECK(iram[i]);
								}
#undef CHECK
							}
							auto *ex = reinterpret_cast<unsigned char *>(&x.eram);
							auto *ey = reinterpret_cast<unsigned char *>(&y.eram);
							size_t offset = offsetof(esp::EramRuntime<12>, readLatch);
							if (std::memcmp(ex + offset, ey + offset, sizeof(x.eram) - offset))
							{
								std::printf("ERAM kind=%u clocks=%u\n", kind, clocks);
								for (size_t q = offset; q < sizeof(x.eram); ++q)
									if (ex[q] != ey[q])
										std::printf(" byte%zu=%02x/%02x", q, ex[q], ey[q]);
								std::puts("");
								match = false;
							}
							if (std::memcmp(x.gram, y.gram, sizeof(x.gram)) ||
								std::memcmp(x.mulcoeffs, y.mulcoeffs, sizeof(x.mulcoeffs)) ||
								std::memcmp(x.eram.memory.get(), y.eram.memory.get(), 4096 * 4) ||
								std::memcmp(x.eram.packed.get(), y.eram.packed.get(), 4096 * 2))
							{
								std::printf("memory kind=%u clocks=%u\n", kind, clocks);
								match = false;
							}
							if (std::memcmp(x.mailbox, y.mailbox, sizeof(x.mailbox)) ||
								x.irq1History != y.irq1History || x.irq1Request != y.irq1Request ||
								x.publishedDint != y.publishedDint || x.frameCycle != y.frameCycle ||
								x.framePaused != y.framePaused)
							{
								std::puts("envelope/mailbox mismatch");
								match = false;
							}
							++checks;
							if (!match)
							{
								++failures;
								std::printf("FAILED mode=%x kind=%u clocks=%u\n", mode, kind, clocks);
							}
						}
	std::printf("checks=%u failures=%u\n", checks, failures);
	return failures ? 1 : 0;
}

#else
int main()
{
	return 77;
}
#endif
