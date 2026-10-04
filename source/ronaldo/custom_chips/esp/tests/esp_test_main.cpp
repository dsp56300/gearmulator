// ESP core tests. The recorded hardware corpus (silicon expectations captured on
// the ESP2 rig, tools/esp_hardware) is replayed on the canonical interpreter.
// Native kernels match the sequential/arbitrated interpreter on random programs
// per feature and live host-write mixes; dispatcher behavior has separate checks.
// --quick skips the whole corpus; --jit runs the engine sections alone;
// --inhibit runs the output-cleared fast path against the full interpreter alone;
// --replay <dump> [frames] [jit|fast] replays a diverging program.

#include "custom_chips/esp/esp_jit_backend.h"

#include "fixtures/esp_hardware_fixtures.h"
#include "fixtures/esp_host_fixtures.h"
#include "fixtures/esp_queue_host_fixtures.h"
#include "fixtures/esp_pipeline_fixtures.h"
#include "fixtures/esp_firmware_eram_fixtures.h"
#include "fixtures/esp_config_fixtures.h"
#include "fixtures/esp_queue_config_fixtures.h"
#include "fixtures/esp_broadcast_fixtures.h"
#include "fixtures/esp_audit_fixtures.h"
#include "fixtures/esp_jp_overlap_fixtures.h"
#include "fixtures/esp_partial_read_fixtures.h"
#include "fixtures/esp_rotation_fixtures.h"
#include "fixtures/esp_rotation_entry_fixtures.h"
#include "fixtures/esp_rotation_memory_entry_fixtures.h"
#include "fixtures/esp_inhibit_fixtures.h"
#include "fixtures/esp_tail_fixtures.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace
{
	using esp::se;
	struct Snapshot;
	template <int N> Snapshot snapInterpreter(esp::Interpreter<N>& e);
	// packed[i] must mirror memory[i] whenever compression is on.
	template <int N> bool shadowConsistent(const esp::Runtime<N>& rt)
	{
		if (!rt.eram.compress) return true;
		for (unsigned a = 0; a < rt.eram.size; ++a) if (rt.eram.packed[a] != esp::packEram(rt.eram.memory[a])) return false;
		return true;
	}

	// Engine the reference-vs-new sections run on (the dynamic interpreter is
	// the engine of record; the compiled engines must be indistinguishable).
	esp::Dispatcher<17>::Engine g_engine = esp::Dispatcher<17>::Engine::Interpreter;
	std::unique_ptr<esp::Dispatcher<17>> newDispatcher()
	{
		auto d = std::make_unique<esp::Dispatcher<17>>();
		d->setEngine(g_engine);
		d->setBackgroundCompile(false);
		return d;
	}
	unsigned g_failed = 0, g_tested = 0;

	int32_t rigInput(uint8_t slot) { return slot < 0x60 ? 0x111110 : 0xffffc0; }

	uint32_t word(unsigned op, unsigned m, unsigned s, unsigned c, unsigned field = 0) { return (op << 16) | (m << 10) | (s << 8) | (c & 0xff) | (field << 23); }
	struct Program { uint32_t w[2][768] = {}; };

	struct Snapshot { std::vector<std::pair<std::string, int64_t>> v; void add(const std::string& n, int64_t x) { v.emplace_back(n, x); } };
	std::mt19937 g_rng(12345);
	unsigned rnd(unsigned n) { return g_rng() % n; }
	const unsigned macOps[] = {0x00,0x04,0x08,0x0c,0x10,0x14,0x18,0x1c,0x40,0x44,0x48,0x4c,0x58,0x5c,0x60,0x64,0x68,0x6c,0x70,0x74,0x78,0x7c};
	uint32_t macWord() { return word(macOps[rnd(22)], rnd(256), rnd(4), rnd(256)); }
	void fillMac(Program& p, int core, int from, int to) { for (int i = from; i < to; ++i) p.w[core][i] = rnd(8) ? macWord() : 0; }

	// One program word for a diagnostic line: index, hex, decoded class.
	void printWord(FILE* f, unsigned core, unsigned pc, uint32_t w)
	{
		static const char* const names[] = {"nop", "mac", "storeSat", "gramRead", "gramStoreReplace", "gramStoreAdd", "mul30", "special", "storeRaw", "storeRect", "cmp", "op54", "invert", "storeFoldPos", "storeFoldNeg"};
		const esp::Instr in(w);
		const unsigned k = unsigned(in.op);
		std::fprintf(f, "%u:%03x %08x %s mem=%02x coef=%02x shift=%u%s%s\n", core, pc, w, k < std::size(names) ? names[k] : "?", in.mem, uint8_t(in.coef), in.shift, in.accB ? " B" : " A", in.replace ? " replace" : "");
	}

	// Divergent program as a replay file (--replay <file>): config, program,
	// seed, clocks, input flag. Path esp_diverge_<category>.bin in the cwd.
	void dumpDivergence(const char* name, const Program& prog, const uint8_t* cfg, unsigned p, unsigned clocks, bool withInput)
	{
		std::string path = "esp_diverge_"; for (const char* s = name; *s; ++s) path += std::isalnum(uint8_t(*s)) ? *s : '_';
		path += ".bin";
		FILE* f = std::fopen(path.c_str(), "wb");
		if (!f) return;
		std::fwrite(cfg, 1, 8, f); std::fwrite(prog.w, sizeof(uint32_t), 2 * 768, f);
		const uint32_t meta[3] = {p, clocks, withInput ? 1u : 0u}; std::fwrite(meta, sizeof(uint32_t), 3, f);
		std::fclose(f);
	}

	bool g_replayFrameLevel = false, g_replayFast = false;

	// Self-consistency of the interpreter: a frame run in one call must equal
	// the same frame run clock by clock (the fast-forward paths are the only
	// difference). Finds the smallest clock count where they part.
	int replayFastForward(const uint8_t* cfg, const Program& prog, unsigned p, unsigned clocks, bool withInput, unsigned frames)
	{
		auto build = [&]()
		{
			auto e = std::make_unique<esp::Dispatcher<17>>();
			e->setEngine(esp::Dispatcher<17>::Engine::Interpreter);
			e->setExternalClockPresent(true);
			if (withInput) e->setReceiveInput(rigInput);
			for (unsigned i = 0; i < 8; ++i) e->hostWrite(0x2000 + i, cfg[i]);
			for (int c = 0; c < 2; ++c) for (int i = 0; i < 768; ++i) e->writePMem32(c * 0x400 + i, prog.w[c][i]);
			std::mt19937 seed(p * 7919 + 1);
			for (int i = 0; i < 256; ++i)
			{
				const int32_t g = se<24>(int32_t(seed())), a = se<24>(int32_t(seed())), b = se<24>(int32_t(seed()));
				e->writeGRAM(g, i); e->writeIRAM0(a, i); e->writeIRAM1(b, i);
			}
			return e;
		};
		for (unsigned fr = 0; fr < frames; ++fr)
		{
			for (unsigned k = 1; k <= clocks; ++k)
			{
				auto batched = build(), stepped = build();
				for (unsigned f = 0; f < fr; ++f) { for (auto* e : {batched.get(), stepped.get()}) { e->runProgram(clocks); e->sync(); } }
				batched->runProgram(k);
				for (unsigned i = 0; i < k; ++i) stepped->runProgram(1);
				const auto sb = snapInterpreter(batched->interpreter()), ss = snapInterpreter(stepped->interpreter());
				bool div = false;
				for (size_t i = 0; i < sb.v.size(); ++i)
					if (sb.v[i].second != ss.v[i].second)
					{
						if (!div) std::printf("frame %u: batched(%u clocks) != stepped\n", fr, k);
						div = true;
						std::printf("  %s batched=%lld stepped=%lld\n", sb.v[i].first.c_str(), (long long)sb.v[i].second, (long long)ss.v[i].second);
					}
				if (div)
				{
					for (unsigned c = 0; c < 2; ++c)
					{
						const uint32_t pc = stepped->runtime().core[c].pc;
						std::printf("  core %u pc now %u (batched %u):\n", c, pc, batched->runtime().core[c].pc);
						for (int d = -3; d <= 1; ++d) if (int(pc) + d >= 0 && int(pc) + d < 768) { std::printf("    "); printWord(stdout, c, unsigned(int(pc) + d), prog.w[c][int(pc) + d]); }
					}
					return 1;
				}
			}
		}
		std::printf("batched and stepped agree over %u frames\n", frames);
		return 0;
	}

	// Clock-by-clock replay of a dumped program: first clock where the
	// interpreter leaves the reference, with the fields and both cores' words.
	template <int N> Snapshot snapInterpreter(esp::Interpreter<N>& e)
	{
		Snapshot s;
		auto& rt = e.runtime();
		for (int i = 0; i < 2; ++i)
		{
			auto& k = rt.core[i];
			const std::string p = "core" + std::to_string(i);
			s.add(p + ".A", k.accA.rawFull()); s.add(p + ".B", k.accB.rawFull());
			for (int j = 0; j < 3; ++j) { s.add(p + ".Ahist", k.accA.hist[(k.accA.head + j) % 3]); s.add(p + ".Bhist", k.accB.hist[(k.accB.head + j) % 3]); }
			s.add(p + ".head", k.accA.head);
			for (int j = 0; j < 256; ++j) s.add(p + ".iram[" + std::to_string(j) + "]", k.readIram(j));
			s.add(p + ".lastMulA", k.lastMulInputA); s.add(p + ".lastMul30", k.lastMul30); s.add(p + ".transform", k.lastCoefficientTransform);
			s.add(p + ".skipPos", k.skipfieldPos); s.add(p + ".skipNeg", k.skipfieldNeg); s.add(p + ".predReady", k.predicateReady); s.add(p + ".predValues", k.predicateValues);
			s.add(p + ".cmpPending", k.comparePending); s.add(p + ".cmpNeg", k.compareNegative);
			s.add(p + ".pc", k.pc); s.add(p + ".pcjumpat", k.pcjumpat);
			s.add(p + ".dintPin", k.dintPin); s.add(p + ".irq1Sign", k.irq1Sign); s.add(p + ".iramPos", k.iramPos);
		}
		for (int j = 0; j < 256; ++j) s.add("gram[" + std::to_string(j) + "]", rt.readGram(j));
		for (int j = 0; j < 8; ++j) s.add("coef" + std::to_string(j), rt.mulcoeffs[j]);
		s.add("mailbox", e.readHostReg());
		auto& er = rt.eram;
		s.add("eramRead", er.readLatch); s.add("eramWrite", er.writeLatch); s.add("eramVar", er.varOffset);
		s.add("eramPos", er.pos); s.add("eramActive", er.active()); s.add("eramReadPacked", er.readPacked); s.add("eramHigh", er.highOffset);
		s.add("eramEff", er.effectiveAddr); s.add("eramLowEff", er.lowEffectiveAddr); s.add("eramWLN", er.writeLatchNext); s.add("eramWPN", er.writePackedNext);
		s.add("eramRPN", er.readPackedNext); s.add("eramRRN", er.readRawNext); s.add("eramVON", er.varOffsetNext);
		s.add("eramCycle", int64_t(er.cycle)); s.add("eramNextCmd", int64_t(er.nextCommandCycle)); s.add("eramStartCur", int64_t(er.cycleStartCurrent));
		s.add("eramCommit", int64_t(er.cycleCommit)); s.add("eramMode", er.modeCurrent); s.add("eramShort", er.shortCurrent);
		for (unsigned a = 0; a < 4096; ++a) s.add("eram[" + std::to_string(a) + "]", er.memory[a]);
		s.add("dint", e.readDint());
		return s;
	}

	struct FrameWrites { std::vector<std::pair<uint32_t, uint8_t>> w; };

	// ---------- compiled engines versus the dynamic interpreter ----------
	// Identical starting images, compared after each frame. Native-eligible frames
	// use the sequential/arbitrated oracle; other frames retain lockstep semantics.
	unsigned g_engineFrames = 0, g_engineNative = 0;
	void engineCategory(esp::Dispatcher<17>::Engine engine, const char* name, unsigned count, const std::function<void(Program&)>& gen,
		unsigned frames = 6, unsigned clocksPerFrame = 768, const uint8_t* cfg = nullptr, bool liveWrites = false)
	{
		static const uint8_t baseline[8] = {0xaa, 0x00, 0x90, 0x54, 0xc7, 0, 0, 0};
		if (!cfg) cfg = baseline;
		unsigned diverged = 0, refused = 0;
		for (unsigned p = 0; p < count; ++p)
		{
			Program prog; gen(prog);
			auto r = std::make_unique<esp::Dispatcher<17>>(); auto v = std::make_unique<esp::Dispatcher<17>>();
			r->setEngine(esp::Dispatcher<17>::Engine::Interpreter);
			v->setEngine(engine); v->setBackgroundCompile(false);
			r->setExternalClockPresent(true); v->setExternalClockPresent(true);
			for (unsigned i = 0; i < 8; ++i) { r->hostWrite(0x2000 + i, cfg[i]); v->hostWrite(0x2000 + i, cfg[i]); }
			for (int c = 0; c < 2; ++c) for (int i = 0; i < 768; ++i) { r->writePMem32(c * 0x400 + i, prog.w[c][i]); v->writePMem32(c * 0x400 + i, prog.w[c][i]); }
			std::mt19937 seed(p * 7919 + 1);
			for (int i = 0; i < 256; ++i)
			{
				const int32_t g = se<24>(int32_t(seed())), a = se<24>(int32_t(seed())), b = se<24>(int32_t(seed()));
				for (auto* e : {r.get(), v.get()}) { e->writeGRAM(g, i); e->writeIRAM0(a, i); e->writeIRAM1(b, i); }
			}
			++g_tested;
			bool div = false;
			std::vector<FrameWrites> writes(frames);
#if CHIPS_JIT_ASMJIT
			asmjit::JitRuntime nativeRuntime;
			esp::KernelFn kernel = nullptr;
			uint64_t generation = ~0ull;
#endif
			for (unsigned f = 0; f < frames && !div; ++f)
			{
				if (liveWrites && f)
				{
					const unsigned mode = rnd(3);
					writes[f].w.emplace_back(0x2003, uint8_t(0x50 | mode));
					const unsigned addr = (rnd(2) ? 0x1000 : 0) + rnd(700) * 4;
					for (unsigned b = 0; b < 4; ++b) writes[f].w.emplace_back(addr + b, uint8_t(rnd(256)));
					for (const auto& [a, d] : writes[f].w) { r->hostWrite(a, d); v->hostWrite(a, d); }
				}
				bool native = false;
#if CHIPS_JIT_ASMJIT
				auto& interpreter = v->interpreter();
				const auto& er = v->runtime().eram;
				if (interpreter.batchedFrameReady() && !er.active() && !er.deferredCommand && er.nextCommandCycle <= er.cycle)
				{
					if (generation != interpreter.structureGeneration())
					{
						if (kernel) nativeRuntime.release(kernel);
						kernel = nullptr;
						try
						{
							kernel = esp::JitBackend<17>::compile(nativeRuntime, esp::ProgramSnapshot::take(interpreter), clocksPerFrame);
							if (!kernel) { ++g_failed; std::fprintf(stderr, "native emission failed: %s program %u\n", name, p); }
						}
						catch (const esp::CoreProgram::CompileLimit&) { ++refused; }
						generation = interpreter.structureGeneration();
					}
					if (kernel)
					{
						r->interpreter().runSequential(clocksPerFrame);
						esp::KernelFrame image;
						image.image = esp::makeJitFrame(interpreter);
						kernel(&image);
						interpreter.finishBatchedFrame(clocksPerFrame);
						native = true;
					}
				}
#endif
				if (!native)
				{
					r->interpreter().runProgram(clocksPerFrame);
					v->interpreter().runProgram(clocksPerFrame);
				}
				r->sync(); v->sync();
				++g_engineFrames; g_engineNative += native;
				const auto sr = snapInterpreter(r->interpreter()), sv = snapInterpreter(v->interpreter());
				if (!shadowConsistent(v->runtime())) { div = true; ++diverged; if (diverged <= 3) std::fprintf(stderr, "  engine %s program %u frame %u: packed ERAM shadow inconsistent\n", name, p, f); }
				for (size_t i = 0; i < sr.v.size() && !div; ++i)
					if (sr.v[i].second != sv.v[i].second)
					{
						div = true; ++diverged;
						if (diverged <= 3) std::fprintf(stderr, "  engine %s program %u frame %u: %s interpreter=%lld engine=%lld\n", name, p, f, sr.v[i].first.c_str(), (long long)sr.v[i].second, (long long)sv.v[i].second);
						if (diverged == 1) dumpDivergence(name, prog, cfg, p, clocksPerFrame, false);
						break;
					}
			}
		}
		g_failed += diverged;
		std::printf("%-8s %-22s programs=%u diverged=%u compile-limited=%u\n", "jit", name, count, diverged, refused);
	}

	void enginePrograms(esp::Dispatcher<17>::Engine engine)
	{
		using E = esp::Dispatcher<17>::Engine;
		auto cat = [&](const char* name, unsigned count, const std::function<void(Program&)>& gen, unsigned frames = 6, unsigned clocks = 768, const uint8_t* cfg = nullptr, bool live = false)
		{ engineCategory(engine, name, count, gen, frames, clocks, cfg, live); };
		cat("mac/iram/store", 100, [](Program& p) { fillMac(p, 0, 0, 700); fillMac(p, 1, 0, 700); });
		cat("mac 384-clock frame", 60, [](Program& p) { fillMac(p, 0, 0, 700); fillMac(p, 1, 0, 700); }, 6, 384);
		cat("gram both cores", 100, [](Program& p) {
			const unsigned g[] = {0x20, 0x24, 0x28, 0x2c, 0x38, 0x3c};
			for (int c = 0; c < 2; ++c) for (int i = 0; i < 700; ++i) p.w[c][i] = rnd(3) ? macWord() : word(g[rnd(6)], rnd(256), rnd(4), rnd(256)); });
		cat("cmp", 100, [](Program& p) { fillMac(p, 1, 0, 700); for (int i = 0; i < 700; ++i) p.w[0][i] = rnd(6) ? macWord() : word(0x50, rnd(256), rnd(4), rnd(256)); });
		cat("cmp dense", 60, [](Program& p) { for (int c = 0; c < 2; ++c) for (int i = 0; i < 760; ++i) p.w[c][i] = rnd(3) ? macWord() : word(0x50, rnd(256), rnd(4), rnd(256)); });
		cat("mul30/dmac/coef regs", 100, [](Program& p) {
			for (int i = 0; i < 700; ++i)
			{
				for (int c = 0; c < 2; ++c) p.w[c][i] = rnd(6) ? macWord() : 0;
				if (rnd(9) == 0) { const int idx = rnd(8); p.w[rnd(2)][i] = word(0x34, 0xa0 + idx * 2 + rnd(2), rnd(4), rnd(256)); }
				else if (i + 1 < 700 && rnd(5) == 0) { const int c = rnd(2); p.w[c][i] = word(0x30, rnd(256), rnd(4), rnd(256)); if (rnd(2)) p.w[c][i + 1] = word(0x34, 0xc6 + (rnd(2) ? 0x20 : 0) + (rnd(2) ? 0x10 : 0), rnd(4), rnd(256)); }
				if (rnd(12) == 0) p.w[1][i] = word(0x34, 0xc7 + (rnd(2) ? 0x20 : 0), rnd(4), rnd(256));
			} });
		cat("specials/mailbox/irq", 60, [](Program& p) {
			for (int c = 0; c < 2; ++c) for (int i = 0; i < 700; ++i) p.w[c][i] = rnd(8) ? macWord() : word(0x34, 0xc0 + 4 + rnd(12) + (rnd(2) ? 0x20 : 0) + (rnd(2) ? 0x10 : 0), rnd(4), rnd(256)); });
		auto eramCmd = [](Program& p, int pc, unsigned cmd, uint32_t imm) { for (int k = 0; k < 5; ++k) if (pc + k < 768) p.w[1][pc + k] |= (k == 0 ? cmd : ((imm >> ((k - 1) * 5)) & 31)) << 23; };
		auto eramProg = [eramCmd](Program& p, int minPc, int maxPc, int spacing, bool lowBits, bool variable, bool sourceMoves) {
			fillMac(p, 0, 0, 700); fillMac(p, 1, 0, 700);
			for (int c = 0; c < 2; ++c) for (int i = 0; i < 768; ++i) p.w[c][i] &= 0x7fffff;
			for (int pc = minPc; pc + 12 < maxPc; pc += spacing + rnd(4))
			{
				const bool write = rnd(2);
				unsigned cmd = variable && !write ? 0x18 : write ? 0x10 : 0x08;
				if (lowBits) cmd |= rnd(8);
				const uint32_t imm = variable ? (rnd(2) ? 1 : 0) | (rnd(2) ? 2 : 0) | (rnd(2) ? 0x100 : 0) | (rnd(32) << 12) : rnd(1 << 17);
				if (variable) p.w[1][pc - 1] = word(0x34, 0xc7, 0, 0);
				if (write) p.w[1][pc + (sourceMoves ? 1 + rnd(8) : 1)] = word(0x34, 0xcb + (rnd(2) ? 0x20 : 0), 0, 0);
				eramCmd(p, pc, cmd, imm);
				p.w[1][pc + 5 + rnd(7)] = word(0x34, 0xcc + rnd(4) + (rnd(2) ? 0x20 : 0), 0, 0);
			}
		};
		cat("eram fixed", 100, [&](Program& p) { eramProg(p, 8, 740, 14, false, false, false); }, 8);
		cat("eram variable", 100, [&](Program& p) { eramProg(p, 8, 740, 14, false, true, false); }, 8);
		cat("eram source moves", 60, [&](Program& p) { eramProg(p, 8, 740, 14, false, false, true); }, 8);
		cat("eram dense spacing", 100, [&](Program& p) { eramProg(p, 8, 740, 4, false, rnd(2), true); }, 8);
		cat("eram frame boundary", 60, [&](Program& p) { eramProg(p, 8, 700, 14, false, false, false); eramCmd(p, 744 + rnd(24), rnd(2) ? 0x10 : 0x08, rnd(1 << 17)); p.w[1][3] = word(0x34, 0xcc, 0, 0); }, 8);
		cat("eram 384-clock frame", 60, [&](Program& p) { eramProg(p, 8, 384, 14, false, rnd(2), true); }, 8, 384);
		static const uint8_t raw34[8] = {0xaa, 0x00, 0x90, 0x34, 0xc7, 0x0f, 0, 0};
		cat("eram raw34", 60, [&](Program& p) { eramProg(p, 8, 740, 8, false, rnd(2), true); }, 8, 768, raw34);
		cat("tail + host window", 60, [](Program& p) { fillMac(p, 0, 0, 768); fillMac(p, 1, 0, 768); });
		cat("live host writes", 40, [&](Program& p) { eramProg(p, 8, 700, 12, false, rnd(2), true); }, 30, 768, nullptr, true);
		static const uint8_t freeze[8] = {0xaa, 0x40, 0x90, 0x54, 0xc7, 0, 0, 0};
		cat("config freeze", 30, [&](Program& p) { eramProg(p, 8, 740, 14, false, false, false); }, 6, 768, freeze);
		// Native branches and loops use the same CFG for every outcome.
		auto branchWord = [](int pc, bool allowBack) {
			const unsigned type = rnd(4);
			int target = allowBack && rnd(6) == 0 ? std::max(0, pc - int(rnd(30))) : std::min(1023, pc + 2 + int(rnd(60)));
			return word(0x34, 0xc0 + type + (rnd(2) ? 0x20 : 0), (target >> 8) & 3, target & 0xff);
		};
		auto branchProg = [&](Program& p, unsigned perCore, bool allowBack, bool cmp) {
			fillMac(p, 0, 0, 700); fillMac(p, 1, 0, 700);
			if (cmp) for (int c = 0; c < 2; ++c) for (int i = 0; i < 700; ++i) if (rnd(10) == 0) p.w[c][i] = word(0x50, rnd(256), rnd(4), rnd(256));
			for (int c = 0; c < 2; ++c) for (unsigned k = 0; k < perCore; ++k) { const int pc = int(rnd(700)); p.w[c][pc] = branchWord(pc, allowBack); }
		};
		cat("branches forward", 100, [&](Program& p) { branchProg(p, 6, false, false); }, 8);
		cat("branches loops", 60, [&](Program& p) { branchProg(p, 4, true, false); }, 8);
		cat("branches + cmp", 60, [&](Program& p) { branchProg(p, 5, true, true); }, 8);
		cat("branches dense", 40, [&](Program& p) { branchProg(p, 24, true, true); }, 8);
		cat("branches + gram stall", 60, [&](Program& p) {
			const unsigned g[] = {0x20, 0x24, 0x28, 0x2c, 0x38, 0x3c};
			for (int c = 0; c < 2; ++c) for (int i = 0; i < 700; ++i) p.w[c][i] = rnd(3) ? macWord() : word(g[rnd(6)], rnd(256), rnd(4), rnd(256));
			for (int c = 0; c < 2; ++c) for (unsigned k = 0; k < 6; ++k) { const int pc = int(rnd(700)); p.w[c][pc] = branchWord(pc, true); } }, 8);
		cat("branches + eram", 60, [&](Program& p) { eramProg(p, 8, 740, 10, false, rnd(2), true); for (unsigned k = 0; k < 8; ++k) { const int pc = int(rnd(700)); p.w[1][pc] = (p.w[1][pc] & 0xf800000u) | branchWord(pc, true); } for (unsigned k = 0; k < 4; ++k) { const int pc = int(rnd(700)); p.w[0][pc] = branchWord(pc, true); } }, 8);
		static const uint8_t unconditional[8] = {0xaa, 0x00, 0x90, 0x44, 0xc7, 0, 0, 0};
		cat("branches unconditional", 60, [&](Program& p) { branchProg(p, 6, true, true); }, 8, 768, unconditional);
		cat("branches 384-clock", 40, [&](Program& p) { branchProg(p, 6, true, true); }, 8, 384);
		cat("branches live writes", 40, [&](Program& p) { branchProg(p, 6, true, true); }, 30, 768, nullptr, true);
		// Raw single-word ERAM (mode 14).
		static const uint8_t raw14[8] = {0xaa, 0x00, 0x90, 0x14, 0xc7, 0, 0, 0};
		cat("eram raw14 fixed", 80, [&](Program& p) { eramProg(p, 8, 740, 8, false, false, true); }, 8, 768, raw14);
		cat("eram raw14 variable", 80, [&](Program& p) { eramProg(p, 8, 740, 8, false, true, true); }, 8, 768, raw14);
		cat("eram raw14 dense", 60, [&](Program& p) { eramProg(p, 8, 740, 4, false, rnd(2), true); }, 8, 768, raw14);
		cat("eram raw14 + branches", 40, [&](Program& p) { eramProg(p, 8, 740, 8, false, rnd(2), true); for (unsigned k = 0; k < 8; ++k) { const int pc = int(rnd(700)); p.w[1][pc] = (p.w[1][pc] & 0xf800000u) | branchWord(pc, true); } }, 8, 768, raw14);
		std::printf("%-8s frames %u, native %u\n", "jit", g_engineFrames, g_engineNative);
		g_engineFrames = g_engineNative = 0;
	}

	// Model assumptions both cores must share, documented in doc/esp/README.md.
	// A compressed read sampled at its S+5 shows the byte-mixed word when the
	// previous transaction is far away (the isolated-read fixtures), and the
	// previous complete word when another command started within the hold gap:
	// a 2-voice pitch shifter program consumes a first read at the second
	// read's S+5, nine slots later. To be confirmed on the rig.

	// ---------- output-cleared frames (2005 = 60) versus the full interpreter ----------
	// The interpreter runs such a frame's stores only (runInhibitedFrame): every export is zero, so
	// what the frame leaves behind is exact except the arithmetic nothing reads out - accumulators,
	// predicates, multiplier history, the variable ERAM offset and the read path that follows it.
	// Everything else is compared per frame, the whole ERAM included, from random memory contents so
	// a zero written to a wrong address shows. Programs the fast path refuses (branches, one core
	// cleared) must fall back to the full interpreter and match it entirely.
	bool arithmeticOnly(const std::string& name)
	{
		static const char* const suffixes[] = {".A", ".B", ".Ahist", ".Bhist", ".lastMulA", ".lastMul30", ".transform", ".skipPos",
			".skipNeg", ".predReady", ".predValues", ".cmpPending", ".cmpNeg"};
		for (const char* s : suffixes)
		{
			const size_t n = std::strlen(s);
			if (name.size() >= n && name.compare(name.size() - n, n, s) == 0) return true;
		}
		static const char* const eram[] = {"eramRead", "eramVar", "eramReadPacked", "eramEff", "eramLowEff", "eramRPN", "eramRRN", "eramVON"};
		for (const char* s : eram) if (name == s) return true;
		return false;
	}

	void inhibitCategory(const char* name, unsigned count, const std::function<void(Program&)>& gen, const uint8_t* cfg,
		unsigned clocks = 768, bool expectFast = true, bool split = false)
	{
		unsigned diverged = 0;
		for (unsigned p = 0; p < count; ++p)
		{
			Program prog; gen(prog);
			auto r = std::make_unique<esp::Dispatcher<17>>(), v = std::make_unique<esp::Dispatcher<17>>();
			r->setEngine(esp::Dispatcher<17>::Engine::Interpreter);
			v->setEngine(esp::Dispatcher<17>::Engine::Interpreter);
			r->interpreter().setInhibitFastPath(false);
			for (auto* e : {r.get(), v.get()})
			{
				e->setExternalClockPresent(true);
				for (unsigned i = 0; i < 8; ++i) e->hostWrite(0x2000 + i, cfg[i]);
				for (int c = 0; c < 2; ++c) for (int i = 0; i < 768; ++i) e->writePMem32(c * 0x400 + i, prog.w[c][i]);
			}
			std::mt19937 seed(p * 7919 + 1);
			for (int i = 0; i < 256; ++i)
			{
				const int32_t g = se<24>(int32_t(seed())), a = se<24>(int32_t(seed())), b = se<24>(int32_t(seed()));
				for (auto* e : {r.get(), v.get()}) { e->writeGRAM(g, i); e->writeIRAM0(a, i); e->writeIRAM1(b, i); }
			}
			for (unsigned a = 0; a < r->runtime().eram.size; ++a)
			{
				const int32_t m = se<24>(int32_t(seed()));
				r->runtime().eram.memory[a] = v->runtime().eram.memory[a] = m;
			}
			r->runtime().eram.rebuildPacked(); v->runtime().eram.rebuildPacked();
			// Nonzero latches and registers, so a zero export that is not applied shows.
			const int32_t writeLatch = se<24>(int32_t(seed())), mailbox = int32_t(seed());
			int32_t coefficients[8];
			for (auto& c : coefficients) c = se<24>(int32_t(seed()));
			for (auto* e : {r.get(), v.get()})
			{
				auto& rt = e->runtime();
				rt.eram.writeLatch = writeLatch;
				std::memcpy(rt.mailbox, &mailbox, sizeof(rt.mailbox));
				std::memcpy(rt.mulcoeffs, coefficients, sizeof(rt.mulcoeffs));
				for (auto& core : rt.core) { core.dintPin = 1; core.irq1Sign = true; }
			}
			++g_tested;
			const auto fastBefore = v->interpreter().inhibitedFrames();
			bool div = false;
			for (unsigned f = 0; f < 8 && !div; ++f)
			{
				// A split frame hands the fast path's half to the full interpreter, which resumes from its PCs.
				for (auto* e : {r.get(), v.get()})
				{
					if (split) { e->runProgram(clocks / 2); e->runProgram(clocks - clocks / 2); }
					else e->runProgram(clocks);
					e->sync();
				}
				const auto sr = snapInterpreter(r->interpreter()), sv = snapInterpreter(v->interpreter());
				const auto& er = r->runtime().eram;
				const auto& ev = v->runtime().eram;
				std::string what;
				for (size_t i = 0; i < sr.v.size() && what.empty(); ++i)
					if (sr.v[i].second != sv.v[i].second && !(expectFast && arithmeticOnly(sr.v[i].first)))
						what = sr.v[i].first + " interpreter=" + std::to_string(sr.v[i].second) + " fast=" + std::to_string(sv.v[i].second);
				if (what.empty() && std::memcmp(er.memory.get(), ev.memory.get(), er.size * sizeof(int32_t)))
					for (unsigned a = 0; a < er.size; ++a)
						if (er.memory[a] != ev.memory[a]) { what = "eram[" + std::to_string(a) + "] interpreter=" + std::to_string(er.memory[a]) + " fast=" + std::to_string(ev.memory[a]); break; }
				if (what.empty() && !shadowConsistent(v->runtime())) what = "packed ERAM shadow inconsistent";
				if (!what.empty())
				{
					div = true;
					if (++diverged <= 3) std::fprintf(stderr, "  inhibit %s program %u frame %u: %s\n", name, p, f, what.c_str());
					if (diverged == 1) dumpDivergence(name, prog, cfg, p, clocks, false);
				}
			}
			const auto fastFrames = v->interpreter().inhibitedFrames() - fastBefore;
			if (!div && (expectFast ? fastFrames != 8 : fastFrames != 0))
			{
				++diverged;
				std::fprintf(stderr, "  inhibit %s program %u: %llu of 8 frames took the fast path, expected %u\n", name, p,
					(unsigned long long)fastFrames, expectFast ? 8u : 0u);
			}
		}
		g_failed += diverged;
		std::printf("inhibit  %-22s programs=%u diverged=%u\n", name, count, diverged);
	}

	void inhibitFastPath()
	{
		static const uint8_t cleared[8] = {0xaa, 0x00, 0x90, 0x54, 0xc7, 0x60, 0, 0};
		static const uint8_t raw14[8] = {0xaa, 0x00, 0x90, 0x14, 0xc7, 0x60, 0, 0};
		static const uint8_t raw34[8] = {0xaa, 0x00, 0x90, 0x34, 0xc7, 0x6f, 0, 0};
		static const uint8_t core0Only[8] = {0xaa, 0x00, 0x90, 0x54, 0xc7, 0x20, 0, 0};
		const unsigned gram[] = {0x20, 0x24, 0x28, 0x2c, 0x38, 0x3c};
		// Random ERAM commands on core1 with their latch loads and reads, plus stores and specials.
		const auto eramProgram = [&](Program& p, int maxPc, int spacing, bool variable)
		{
			fillMac(p, 0, 0, 700); fillMac(p, 1, 0, 700);
			for (int c = 0; c < 2; ++c) for (int i = 0; i < 768; ++i) p.w[c][i] &= 0x7fffff;
			for (int pc = 8; pc + 12 < maxPc; pc += spacing + int(rnd(4)))
			{
				const bool write = rnd(2);
				const unsigned cmd = variable && !write && rnd(2) ? 0x18 : write ? 0x10 : 0x08;
				const uint32_t imm = cmd == 0x18 ? (rnd(2) ? 1 : 0) | (rnd(2) ? 2 : 0) | (rnd(2) ? 0x100 : 0) | (rnd(32) << 12) : rnd(1 << 17);
				if (cmd == 0x18) p.w[1][pc - 1] = word(0x34, 0xc7 + (rnd(2) ? 0x20 : 0), 0, 0);
				if (write) p.w[1][pc + 1 + int(rnd(8))] = word(0x34, 0xcb + (rnd(2) ? 0x20 : 0), 0, 0);
				for (int k = 0; k < 5; ++k) p.w[1][pc + k] |= (k == 0 ? cmd : ((imm >> ((k - 1) * 5)) & 31)) << 23;
				p.w[1][pc + 5 + int(rnd(7))] = word(0x34, 0xcc + rnd(4) + (rnd(2) ? 0x20 : 0), 0, 0);
			}
			for (int c = 0; c < 2; ++c) for (int i = 0; i < 700; ++i)
				if (rnd(10) == 0) p.w[c][i] = (p.w[c][i] & 0xf800000u) | word(gram[rnd(6)], rnd(256), rnd(4), rnd(256));
		};
		inhibitCategory("mac/iram/store", 60, [](Program& p) { fillMac(p, 0, 0, 768); fillMac(p, 1, 0, 768); }, cleared);
		inhibitCategory("gram + stalls", 60, [&](Program& p) {
			for (int c = 0; c < 2; ++c) for (int i = 0; i < 700; ++i) p.w[c][i] = rnd(3) ? macWord() : word(gram[rnd(6)], rnd(256), rnd(4), rnd(256)); }, cleared);
		inhibitCategory("coef regs/mul30/cmp", 60, [](Program& p) {
			for (int c = 0; c < 2; ++c) for (int i = 0; i < 700; ++i)
			{
				const unsigned k = rnd(10);
				p.w[c][i] = k == 0 ? word(0x34, 0xa0 + rnd(16), rnd(4), rnd(256)) : k == 1 ? word(0x30, rnd(256), rnd(4), rnd(256))
					: k == 2 ? word(0x50, rnd(256), rnd(4), rnd(256)) : macWord();
			} }, cleared);
		inhibitCategory("specials/mailbox/irq", 60, [](Program& p) {
			for (int c = 0; c < 2; ++c) for (int i = 0; i < 700; ++i)
				p.w[c][i] = rnd(6) ? macWord() : word(0x34, 0xc4 + rnd(12) + (rnd(2) ? 0x20 : 0) + (rnd(2) ? 0x10 : 0), rnd(4), rnd(256)); }, cleared);
		inhibitCategory("eram fixed", 60, [&](Program& p) { eramProgram(p, 740, 14, false); }, cleared);
		inhibitCategory("eram variable", 60, [&](Program& p) { eramProgram(p, 740, 14, true); }, cleared);
		inhibitCategory("eram dense", 60, [&](Program& p) { eramProgram(p, 760, 4, true); }, cleared);
		inhibitCategory("eram 384-clock", 40, [&](Program& p) { eramProgram(p, 384, 8, true); }, cleared, 384);
		inhibitCategory("eram raw14", 40, [&](Program& p) { eramProgram(p, 740, 6, true); }, raw14);
		inhibitCategory("eram raw34", 40, [&](Program& p) { eramProgram(p, 740, 8, true); }, raw34);
		inhibitCategory("eram split frame", 40, [&](Program& p) { eramProgram(p, 740, 8, true); }, cleared, 768, true, true);
		inhibitCategory("fallback: branches", 30, [&](Program& p) {
			eramProgram(p, 740, 14, false);
			const int pc = int(rnd(700)), target = std::min(1023, pc + 2 + int(rnd(60)));
			p.w[0][pc] = word(0x34, 0xc0 + rnd(4), (target >> 8) & 3, target & 0xff); }, cleared, 768, false);
		inhibitCategory("fallback: core0 only", 30, [&](Program& p) { eramProgram(p, 740, 14, false); }, core0Only, 768, false);
	}

	// ---------- semantic unit tests (from the retired core's jump and IRQ tests) ----------
	void expect(bool condition, const char* message)
	{
		++g_tested;
		if (condition) return;
		++g_failed;
		std::fprintf(stderr, "  unit: %s\n", message);
	}

	void unitTests()
	{
		// A coefficient slot can carry multiple values within one frame. Core 0
		// must consume the value published at cycle 4, not the final one at 12.
		{
			auto reference = newDispatcher(), candidate = newDispatcher();
			reference->setEngine(esp::Engine::Interpreter);
			for (auto* e : {reference.get(), candidate.get()})
			{
				e->setExternalClockPresent(true);
				e->hostWrite(0x2003, 0x54);
				e->writeIRAM0(0x100000, 0x10);
				e->writeIRAM1(0x10000, 0x10);
				e->writeIRAM1(0x20000, 0x11);
				e->writePMem32(0x400, word(4, 0x10, 1, 0x40));
				e->writePMem32(0x404, word(0x34, 0xa0, 0, 0));
				e->writePMem32(0x408, word(4, 0x11, 1, 0x40));
				e->writePMem32(0x40c, word(0x34, 0xa0, 0, 0));
				e->writePMem32(6, word(0x30, 0x10, 0, 0));
				e->writePMem32(10, word(0x40, 0x20, 0, 0));
				if (e == candidate.get()) e->interpreter().runSequential(16);
				else e->runProgram(16);
			}
			expect(reference->readIRAM0(0x20) == 8192 && candidate->readIRAM0(0x20) == 16384,
				"sequential scheduling loses an intermediate shared coefficient value");
		}
		{
			auto reference = newDispatcher(), candidate = newDispatcher();
			reference->setEngine(esp::Engine::Interpreter);
			for (auto* e : {reference.get(), candidate.get()})
			{
				e->setExternalClockPresent(true);
				e->hostWrite(0x2003, 0x54);
				e->writeGRAM(0x100000, 0x20);
				for (unsigned c = 0; c < 2; ++c) e->writePMem32(c * 0x400 + 4, word(0x24, 0x20, 0, 0x7f));
				if (e == candidate.get()) e->interpreter().runSequential(16);
				else e->runProgram(16);
			}
			expect(snapInterpreter(reference->interpreter()).v == snapInterpreter(candidate->interpreter()).v,
				"whole-core scheduling can replay core-1 GRAM arbitration stalls");
		}
		// Whole-core scheduling is valid for independent programs, but a GRAM
		// producer later in the frame must not become visible to an earlier read.
		for (const unsigned producer : {0u, 1u})
		{
			for (const bool shared : {false, true})
			{
				auto reference = newDispatcher(), candidate = newDispatcher();
				reference->setEngine(esp::Engine::Interpreter);
				const unsigned consumer = producer ^ 1;
				for (auto* e : {reference.get(), candidate.get()})
				{
					e->setExternalClockPresent(true);
					e->hostWrite(0x2003, 0x54);
					e->writeIRAM0(0x100000, 0x10);
					e->writeIRAM1(0x100000, 0x10);
					e->writePMem32(producer * 0x400, word(4, 0x10, 0, 0x7f));
					e->writePMem32(producer * 0x400 + 8, word(shared ? 0x28 : 0x08, 0x20, 0, 0));
					e->writePMem32(consumer * 0x400 + 4, word(shared ? 0x24 : 4, shared ? 0x20 : 0x10, 0, 0x7f));
					if (e == candidate.get()) e->interpreter().runSequential(16);
					else e->runProgram(16);
				}
				const bool equal = snapInterpreter(reference->interpreter()).v == snapInterpreter(candidate->interpreter()).v;
				expect(equal == (!shared || producer == 0), "sequential scheduling preserves the expected GRAM visibility");
			}
		}
		// Four recurring branch paths, followed by a program replacement and
		// an irregular input sequence. Compare the whole image on every frame,
		// including frames rendered while the background compiler catches up.
		if (g_engine != esp::Engine::Interpreter) for (bool background : {false, true})
		{
			auto reference = newDispatcher(), candidate = newDispatcher();
			reference->setEngine(esp::Engine::Interpreter);
			candidate->setBackgroundCompile(background);
			for (auto* e : {reference.get(), candidate.get()})
			{
				e->setExternalClockPresent(true);
				e->hostWrite(0x2003, 0x54);
				for (unsigned core : {0u, 0x400u})
				{
					e->writePMem32(core, word(4, 0x10, 0, 0x7f));
					e->writePMem32(core + 4, word(0x34, 0xc1, 0, 12));
					e->writePMem32(core + 8, word(0x08, 0x20, 0, 0));
				}
			}
			bool equal = true;
			uint64_t compiledPrograms = 0;
			for (unsigned frame = 0; frame < 160 && equal; ++frame)
			{
				for (auto* e : {reference.get(), candidate.get()})
				{
					if (frame == 96) e->writePMem32(4, word(0x34, 0xc2, 0, 16));
					const unsigned pattern = frame < 128 ? frame : (frame * frame / 7);
					e->writeIRAM0((pattern & 1) ? -0x100000 : 0x100000, 0x10);
					e->writeIRAM1((pattern & 2) ? -0x100000 : 0x100000, 0x10);
					e->runProgram(); e->sync();
				}
				equal = snapInterpreter(reference->interpreter()).v == snapInterpreter(candidate->interpreter()).v;
				if (frame == 64) compiledPrograms = candidate->statistics().compiles;
				if (frame == 95 && !background)
					expect(candidate->statistics().compiles == compiledPrograms, "four branch paths share one kernel");
				// Let the worker run without relying on emulator speed or a frame
				// count as a compilation deadline.
				if (background) std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			expect(equal, "native branches and program replacement preserve the full interpreter image");
			expect(candidate->statistics().native > 0, "branching dispatcher executes compiled frames");
			if (background) expect(candidate->statistics().adopted > 0, "background kernels are adopted");
		}
		{
			struct ScheduleProbe : esp::Base<0>
			{
				bool hashMatches() const { return m_scheduleHash == scheduleHash(); }
			};
			auto probe = std::make_unique<ScheduleProbe>();
			// The command pattern includes both immediate and deferred starts.
			for (unsigned pc = 0; pc < 800; ++pc)
				probe->writePMem32(0x400 + pc, ((pc * 13u) & 31u) << 23);
			for (uint8_t bus : {uint8_t(0), uint8_t(0x10)})
				for (uint8_t memory : {uint8_t(0), uint8_t(0x20), uint8_t(0x40), uint8_t(0x60)})
				{
					probe->hostWrite(0x2005, bus);
					for (unsigned mode = 0; mode < 4; ++mode)
					{
						probe->hostWrite(0x2003, memory | mode);
						expect(probe->hashMatches(), "host modes and command spacing retain the current schedule hash");
						probe->writePMem32(0x400 + 9 + mode, ((mode * 7) & 31u) << 23);
						expect(probe->hashMatches(), "program field edits refresh the cached schedule");
					}
				}
			probe->reset();
			probe->hostWrite(0x2003, 0x54);
			expect(probe->hashMatches(), "reset discards the previous program schedule");
		}
		constexpr uint32_t setDintInstruction = (0x34u << 16) | (0xc4u << 10);
		constexpr uint16_t core1 = 0x400;
		{
			auto e = newDispatcher();
			expect(e->isEramCompression(), "ERAM compression models the JP-8000 board by default");
			e->setEramCompression(false);
			expect(!e->isEramCompression(), "ERAM compression is configurable per board");
			e->setEramCompression(true);
			// A negative core1 IRAM operand on C4 cannot drive the separate IRQ1 pin.
			e->writePMem32(core1, setDintInstruction);
			e->writeIRAM1(-1, 0xc4);
			e->runProgram(768); e->sync();
			expect(e->readDint() == 2, "core1 C4 leaves IRQ1 idle high");
			e->reset();
			e->writePMem32(0, word(0x04, 3, 0, 0xd5));
			e->writePMem32(4, setDintInstruction);
			e->runProgram(768);
			expect((e->readDint() & 1) == 0, "IRQ0 retains its published level until synchronization");
			e->sync();
			expect((e->readDint() & 1) == 1, "negative selected accumulator publishes IRQ0 high");
			e->writePMem32(0, word(0x04, 3, 0, 43));
			e->runProgram(768); e->sync();
			expect((e->readDint() & 1) == 0, "positive selected accumulator publishes IRQ0 low");
		}
		// CMP at PC 0 establishes the condition; a MAC result is observed three
		// instructions later through a store from the accumulator pipe.
		auto cmpWindow = [](uint8_t compareCoefficient, uint16_t macPc)
		{
			auto e = newDispatcher();
			e->writePMem32(0, word(0x50, 1, 0, compareCoefficient));
			e->writePMem32(macPc, word(0x04, 1, 0, 0x7f));
			e->writePMem32(macPc + 3, word(0x08, 0x20, 0, 0));
			e->runProgram(768);
			return e->readIRAM0(0x20);
		};
		expect(cmpWindow(0xff, 7) == 0, "CMP zeroes the MAC input in the negative skip window");
		expect(cmpWindow(0x01, 5) == 0, "CMP zeroes the MAC input in the positive skip window");
		expect(cmpWindow(0xff, 5) > 0, "CMP preserves the MAC input outside the negative skip window");
		// Branches: A supplies the condition, B the independent IRQ marker;
		// DINT reads the selected delayed accumulator, not IRAM.
		auto conditionalJump = [](uint8_t coefficient, uint8_t jumpType)
		{
			auto e = newDispatcher();
			e->writePMem32(0, word(0x14, 3, 0, 0xff));	// B negative
			e->writePMem32(1, word(4, 1, 0, coefficient));
			e->writePMem32(4, word(0x34, 0xc0 | jumpType, 0, 12));
			e->writePMem32(5, word(0x34, 0xe4, 0, 0));	// first delay slot: IRQ0 high
			e->writePMem32(6, word(0x14, 3, 0, 0x7f));	// second delay slot: B positive
			e->writePMem32(9, word(0x34, 0xe4, 0, 0));	// fallthrough: IRQ0 low
			e->runProgram(768); e->sync();
			return e->readDint() & 1;
		};
		expect(conditionalJump(0, 0) == 1, "zero jump takes the branch after both delay slots");
		expect(conditionalJump(0xff, 1) == 1, "negative jump takes the branch");
		expect(conditionalJump(0x7f, 2) == 1, "positive jump takes the branch");
		expect(conditionalJump(0x7f, 1) == 0, "untaken jump executes the fallthrough IRQ instruction");
		expect(conditionalJump(0, 3) == 1, "unconditional jump skips the fallthrough IRQ instruction");
		{
			auto e = newDispatcher();
			e->writePMem32(0, word(0x14, 3, 0, 0xff));
			e->writePMem32(3, word(0x34, 0xe4, 0, 0));
			e->writePMem32(4, word(0x34, 0xc3, 0, 0));
			e->runProgram(768); e->sync();
			expect((e->readDint() & 1) == 1, "backward branch preserves the IRQ latch within the clock budget");
		}
	}

	// ---------- recorded hardware corpus (expectations from silicon) ----------
	// The exporter marks fixtures the model is known not to reproduce as pending;
	// those are counted, not failed. Fixtures that need the fitted-board physical
	// cell profile (cross-format storage, crossover retention) are not replayed:
	// that profile is not modelled, see doc/esp/README.md.
	unsigned g_pending = 0;
	template <class Fs, class Mask> void corpusSet(const char* name, const Fs& fixtures, Mask maskFor)
	{
		unsigned n = 0, failed = 0, pending = 0;
		for (const auto& f : fixtures)
		{
			auto d = newDispatcher();
			d->setReceiveInput(rigInput);
			for (unsigned i = 0; i < 8; ++i) d->hostWrite(0x2000 + i, f.configuration[i]);
			for (size_t i = 0; i < f.programSize; ++i) d->writePMem32(f.program[i].index, f.program[i].value);
			// 260 frames: a full IRAM rotation plus settling, as the rig captured.
			for (unsigned frame = 0; frame < 260; ++frame) { d->runProgram(768); d->sync(); }
			const uint32_t m = maskFor(f), actual = d->readHostReg() & 0xffffff;
			++n; ++g_tested;
			if ((actual & m) == (f.expected & m)) continue;
			if (f.pendingInterpreter) { ++pending; continue; }
			++failed; if (failed <= 5) std::fprintf(stderr, "  %s: hardware=%06x model=%06x\n", f.name, f.expected, actual);
		}
		g_failed += failed; g_pending += pending;
		std::printf("corpus   %-22s n=%5u failed=%u pending=%u\n", name, n, failed, pending);
	}

	void hardwareCorpus(bool quick)
	{
		const auto full = [](const auto&) { return 0xffffffu; };
		corpusSet("hardware", espHardwareTests::fixtures, full);
		if (quick) return;
		corpusSet("config", espConfigTests::fixtures, full);
		corpusSet("queueConfig", espQueueConfigTests::fixtures, full);
		corpusSet("pipeline", espPipelineTests::fixtures, full);
		corpusSet("partialRead", espPartialReadTests::fixtures, full);
		corpusSet("audit", espAuditTests::fixtures, full);
		corpusSet("jpOverlap", espJpOverlapTests::fixtures, full);
		corpusSet("firmwareEram", espFirmwareEramTests::fixtures, [](const auto& f) { return f.expectedMask; });
	}

	// Host interface: byte order, packing, queues and broadcast, as recorded.
	void hostFixtures()
	{
		unsigned n = 0, failed = 0, pending = 0;
		auto readWord = [](const esp::Dispatcher<17>& d, unsigned byteAddress)
		{
			uint32_t v = 0;
			for (unsigned b = 0; b < 4; ++b) v |= uint32_t(d.readPRAM(byteAddress + b)) << (8 * b);
			return v;
		};
		for (const auto& f : espHostTests::fixtures)
		{
			auto d = newDispatcher();
			for (size_t i = 0; i < f.count; ++i) d->hostWrite(f.writes[i].address, f.writes[i].value);
			bool same = true;
			for (unsigned w = 0; w < 6; ++w) same &= readWord(*d, f.base + w * 4) == f.expected[w];
			++n; ++g_tested;
			if (same) continue;
			if (f.pending) { ++pending; continue; }
			++failed; if (failed <= 5) std::fprintf(stderr, "  host %s differs\n", f.name);
		}
		for (const auto& f : espQueueHostTests::fixtures)
		{
			auto d = newDispatcher();
			for (size_t i = 0; i < f.writeCount; ++i) d->hostWrite(f.writes[i].address, f.writes[i].value);
			bool same = true;
			for (size_t i = 0; i < f.wordCount; ++i) same &= readWord(*d, f.words[i].address) == f.words[i].value;
			++n; ++g_tested; if (!same) { ++failed; if (failed <= 5) std::fprintf(stderr, "  queue host %s differs\n", f.name); }
		}
		for (const auto& f : espBroadcastTests::fixtures)
		{
			auto d = newDispatcher();
			d->hostWrite(0x2004, 7); d->hostWrite(0x2003, 0x54);
			for (unsigned core = 0; core < 2; ++core) for (unsigned w = 0; w < 6; ++w) for (unsigned b = 0; b < 4; ++b)
				d->hostWrite(core * 0x1000 + (f.pc + w) * 4 + b, uint8_t(f.before[core][w] >> (b * 8)));
			d->hostWrite(0x2003, f.mode);
			for (unsigned b = 0; b < 4; ++b) d->hostWrite(f.core * 0x1000 + f.pc * 4 + b, uint8_t(f.payload >> (b * 8)));
			bool same = (f.mode & 3) != 3 || d->readHostReg() == f.selected;
			for (unsigned core = 0; core < 2; ++core) for (unsigned w = 0; w < 6; ++w) same &= readWord(*d, core * 0x1000 + (f.pc + w) * 4) == f.after[core][w];
			++n; ++g_tested; if (!same) { ++failed; if (failed <= 5) std::fprintf(stderr, "  broadcast mode %02x core %u pc %u differs\n", f.mode, f.core, f.pc); }
		}
		g_failed += failed; g_pending += pending;
		std::printf("corpus   %-22s n=%5u failed=%u pending=%u\n", "host interface", n, failed, pending);
	}

	// IRQ0 levels and the 512-clock IRQ1 pulse per configuration.
	void irqFixtures()
	{
		unsigned n = 0, failed = 0;
		for (const auto& f : espConfigTests::irqFixtures)
		{
			esp::Dispatcher<0> d;
			d.setEngine(g_engine); d.setBackgroundCompile(false);
			for (unsigned i = 0; i < 8; ++i) d.hostWrite(0x2000 + i, f.configuration[i]);
			for (size_t i = 0; i < f.programSize; ++i) d.writePMem32(f.program[i].index, f.program[i].value);
			unsigned irq0Edges = 0, pulses = 0, lowStart = 0;
			bool widths = true;
			d.setDintCallback([&](uint32_t cycle, uint8_t before, uint8_t after)
			{
				if ((before ^ after) & 1) ++irq0Edges;
				if ((before & 2) && !(after & 2)) lowStart = cycle;
				if (!(before & 2) && (after & 2)) { ++pulses; widths &= cycle - lowStart == 512; }
			});
			for (unsigned frame = 0; frame < 64; ++frame) { d.runProgram(768); d.sync(); }
			bool match = (d.readHostReg() & 0xffffff) == f.expected && widths;
			if (f.kind == 1) match &= pulses >= 6 && irq0Edges >= 6;
			else match &= pulses == 0 && (f.kind != 0 || irq0Edges == 0);
			++n; ++g_tested; if (!match) { ++failed; if (failed <= 5) std::fprintf(stderr, "  irq %s\n", f.name); }
		}
		g_failed += failed;
		std::printf("corpus   %-22s n=%5u failed=%u pending=0\n", "irq", n, failed);
	}

	// Rotation freeze entry, memory age and the host-selected tail / IRAM
	// inhibit sequences. Observations that the rig's logic analyzer supplied
	// (transaction addresses, committed write values) come from the runtime
	// image: the controller's last effective address after a frame, and the
	// write latch sampled one clock before each write commits.
	void sequenceFixtures()
	{
		unsigned n = 0, failed = 0;
		for (const auto& f : espRotationMemoryEntryTests::fixtures)
		{
			esp::Dispatcher<4> d; d.setEngine(g_engine); d.setBackgroundCompile(false);
			d.setReceiveInput(rigInput);
			const auto initialPosition = (f.before + 299) & 255;
			for (unsigned i = 0; i < ((-initialPosition) & 255); ++i) d.sync();
			for (size_t i = 0; i < f.count; ++i) d.writePMem32(f.program[i].index, f.program[i].value);
			for (unsigned frame = 0; frame < 299; ++frame) { d.runProgram(768); d.sync(); }
			d.runProgram(500);
			const auto tag = d.readGRAM(0xe1);
			d.hostWrite(0x2001, 0x60);
			d.runProgram(268); d.sync(); d.runProgram(500);
			const auto difference = (f.inputWitness ? d.readGRAM(0xe0) : tag - d.readGRAM(0xe0)) & 0xffffff;
			++n; ++g_tested; if (difference != f.expected) { ++failed; if (failed <= 5) std::fprintf(stderr, "  memory entry %s: expected %06x got %06x\n", f.name, f.expected, difference); }
		}
		for (const auto& f : espRotationEntryTests::fixtures)
		{
			esp::Dispatcher<4> d; d.setEngine(g_engine); d.setBackgroundCompile(false);
			const auto frames = (-f.before) & 0x7ffff;
			for (unsigned i = 0; i < (frames & 255); ++i) d.sync();
			for (unsigned i = frames & 255; i < frames; ++i) d.runtime().eram.tickSample();
			for (unsigned i = 0; i < 256; ++i) { d.writeIRAM0(0x1000 + i, uint8_t(i)); d.writeIRAM1(0x2000 + i, uint8_t(i)); d.writeGRAM(0x3000 + i, uint8_t(i)); }
			d.writePMem32(0x400 + 32, 0x10u << 23);
			d.hostWrite(0x2003, f.config2003); d.hostWrite(0x2005, f.config2005); d.hostWrite(0x2001, f.config);
			bool match = esp::freezePosition(f.before, f.mask) == f.after;
			const auto displacement = (f.after - f.before) & 255;
			for (unsigned i = 0; i < 256; ++i)
			{
				match &= d.readIRAM0(uint8_t(i)) == int(0x1000 + ((i + displacement) & 255));
				match &= d.readIRAM1(uint8_t(i)) == int(0x2000 + ((i + displacement) & 255));
				match &= d.readGRAM(uint8_t(i)) == int(0x3000 + ((i + displacement) & 255));
			}
			d.runProgram(48);
			match &= (d.runtime().eram.pos & f.mask) == f.after;
			d.hostWrite(0x2001, f.config); d.sync(); d.runProgram(48);
			match &= (d.runtime().eram.pos & f.mask) == f.after;
			++n; ++g_tested; if (!match) { ++failed; if (failed <= 5) std::fprintf(stderr, "  freeze entry %s\n", f.name); }
		}
		for (const auto& f : espRotationTests::fixtures)
		{
			auto d = newDispatcher();
			const uint8_t config[] = {0xaa, 0, 0x90, 0x57, 0xc7, 0, 0, 0};
			for (unsigned i = 0; i < 8; ++i) d->hostWrite(0x2000 + i, config[i]);
			for (size_t i = 0; i < f.count; ++i) d->writePMem32(f.program[i].index, f.program[i].value);
			bool match = true;
			const auto observe = [&](unsigned phase, unsigned frames, bool frozen)
			{
				uint32_t previous = 0;
				for (unsigned frame = 0; frame < frames; ++frame)
				{
					d->runProgram(768); d->sync();
					const uint32_t address = d->runtime().eram.effectiveAddr;
					if (frame) match &= ((address - previous) & 0x7ffff) == (frozen ? 0u : 0x7ffffu);
					previous = address;
				}
				match &= (d->readHostReg() & 0xffffff) == f.observed[phase];
			};
			observe(0, 300, false);
			d->hostWrite(0x2001, f.config);
			observe(1, 32, true);
			for (unsigned patch = 0; patch < 2; ++patch)
			{
				d->hostWrite(0x2001, f.config); d->hostWrite(0x2003, 0x54);
				for (unsigned w = 0; w < f.patchCount; ++w) for (unsigned b = 0; b < 4; ++b) d->hostWrite(f.patches[patch][w].index * 4 + b, uint8_t(f.patches[patch][w].value >> (b * 8)));
				d->hostWrite(0x2003, 0x57);
				observe(2 + patch * 2, 32, true);
				d->hostWrite(0x2001, 0);
				observe(3 + patch * 2, 300, false);
			}
			++n; ++g_tested; if (!match) { ++failed; if (failed <= 5) std::fprintf(stderr, "  rotation %s\n", f.name); }
		}
		const auto runSequence = [&](const auto& f)
		{
			// Clock by clock on the interpreter: the write latch is sampled one
			// clock before a write transaction commits.
			esp::Dispatcher<17> d; d.setEngine(esp::Engine::Interpreter);
			const uint8_t config[] = {0xaa, 0, 0x90, 0x57, 0xc7, 0, 0, 0};
			for (unsigned i = 0; i < 8; ++i) d.hostWrite(0x2000 + i, config[i]);
			for (size_t i = 0; i < f.count; ++i) d.writePMem32(f.program[i].index, f.program[i].value);
			uint32_t lastWritten = 0, writes = 0;
			bool match = true;
			for (size_t i = 0; i < f.actionCount; ++i)
			{
				const auto& action = f.actions[i];
				if (action.kind == 0) d.hostWrite(action.address, action.value);
				else if (action.kind == 1 || action.kind == 5)
				{
					for (unsigned b = 0; b < 4; ++b) d.hostWrite(action.address + b, uint8_t((action.kind == 5 ? 0 : action.value) >> (b * 8)));
				}
				else
				{
					// Hardware observations follow 12 ms or more of settling.
					for (unsigned frame = 0; frame < 600; ++frame)
					{
						for (unsigned clock = 0; clock < 768; ++clock)
						{
							const auto& e = d.runtime().eram;
							const bool commitNext = e.activeCurrent && e.cycle == e.cycleCommit && (e.modeCurrent & 0x18) == 0x10;
							const int32_t latch = e.writeLatch;
							d.runProgram(1);
							if (commitNext) { lastWritten = uint32_t(latch) & 0xffffff; ++writes; }
						}
						d.sync();
					}
					const uint32_t actual = action.kind == 2 ? d.readHostReg() & 0xffffff
						: action.kind == 3 ? d.readDint()
						: writes ? (lastWritten >> action.address) << action.address : 0x1000000;
					if (actual != action.value) { match = false; if (failed < 5) std::fprintf(stderr, "  sequence %s action %zu: hardware=%06x model=%06x\n", f.name, i, action.value, actual); }
				}
			}
			++n; ++g_tested; if (!match) ++failed;
		};
		for (const auto& f : espInhibitTests::fixtures) runSequence(f);
		for (const auto& f : espTailTests::fixtures) runSequence(f);
		g_failed += failed;
		std::printf("corpus   %-22s n=%5u failed=%u pending=0\n", "sequences", n, failed);
	}

	// ---------- replay of a dumped random program ----------
	int replayDump(const char* path, unsigned frames)
	{
		FILE* f = std::fopen(path, "rb");
		if (!f) { std::fprintf(stderr, "cannot open %s\n", path); return 2; }
		uint8_t cfg[8]; Program prog; uint32_t meta[3];
		if (std::fread(cfg, 1, 8, f) != 8 || std::fread(prog.w, sizeof(uint32_t), 2 * 768, f) != 2 * 768 || std::fread(meta, sizeof(uint32_t), 3, f) != 3) { std::fclose(f); return 2; }
		std::fclose(f);
		const unsigned p = meta[0], clocks = meta[1]; const bool withInput = meta[2] != 0;
		if (g_replayFast) return replayFastForward(cfg, prog, p, clocks, withInput, frames);
		auto r = std::make_unique<esp::Dispatcher<17>>(); r->setEngine(esp::Engine::Interpreter);
		auto v = newDispatcher();
		for (auto* e : {r.get(), v.get()})
		{
			e->setExternalClockPresent(true);
			if (withInput) e->setReceiveInput(rigInput);
			for (unsigned i = 0; i < 8; ++i) e->hostWrite(0x2000 + i, cfg[i]);
			for (int c = 0; c < 2; ++c) for (int i = 0; i < 768; ++i) e->writePMem32(c * 0x400 + i, prog.w[c][i]);
			std::mt19937 seed(p * 7919 + 1);
			for (int i = 0; i < 256; ++i)
			{
				const int32_t g = se<24>(int32_t(seed())), a = se<24>(int32_t(seed())), b = se<24>(int32_t(seed()));
				e->writeGRAM(g, i); e->writeIRAM0(a, i); e->writeIRAM1(b, i);
			}
		}
		for (unsigned fr = 0; fr < frames; ++fr)
		{
			r->runProgram(clocks); v->runProgram(clocks); r->sync(); v->sync();
			const auto sr = snapInterpreter(r->interpreter()), sv = snapInterpreter(v->interpreter());
			for (size_t i = 0; i < sr.v.size(); ++i)
				if (sr.v[i].second != sv.v[i].second) { std::printf("frame %u: %s interpreter=%lld engine=%lld\n", fr, sr.v[i].first.c_str(), (long long)sr.v[i].second, (long long)sv.v[i].second); return 1; }
		}
		std::printf("no divergence in %u frames\n", frames);
		return 0;
	}
}

int main(int argc, char** argv)
{
	const bool quick = argc > 1 && std::strcmp(argv[1], "--quick") == 0;
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	using Engine = esp::Dispatcher<17>::Engine;
	if (argc > 2 && std::strcmp(argv[1], "--replay") == 0)
	{
		if (argc > 4) g_engine = std::strcmp(argv[4], "jit") == 0 ? Engine::Jit : Engine::Interpreter;
		if (argc > 4) g_replayFast = std::strcmp(argv[4], "fast") == 0;
		return replayDump(argv[2], argc > 3 ? unsigned(std::atoi(argv[3])) : 8);
	}
	if (argc > 1 && std::strcmp(argv[1], "--inhibit") == 0)
	{
		inhibitFastPath();
		std::printf("%u checks, %u failures\n", g_tested, g_failed);
		return g_failed ? 1 : 0;
	}
	if (argc > 1 && std::strcmp(argv[1], "--jit") == 0)
	{
#if CHIPS_JIT_ASMJIT
		enginePrograms(Engine::Jit);
#endif
		std::printf("%u checks, %u failures\n", g_tested, g_failed);
		return g_failed ? 1 : 0;
	}
	// Hardware fixtures retain lockstep semantics; native code is compared above
	// with the sequential/arbitrated interpreter, including arbitrary shared-memory programs.
	inhibitFastPath();
#if CHIPS_JIT_ASMJIT
	enginePrograms(Engine::Jit);
#endif
	std::vector<Engine> engines = {Engine::Interpreter};

	for (const Engine engine : engines)
	{
		g_engine = engine;
		std::printf("--- engine %s ---\n", "interpreter");
		unitTests();
		hostFixtures();
		irqFixtures();
		hardwareCorpus(quick);
		if (!quick || engine == Engine::Interpreter) sequenceFixtures();
	}
#if CHIPS_JIT_ASMJIT
	g_engine = Engine::Jit;
	unitTests();
#endif
	std::printf("%u checks, %u failures, %u pending\n", g_tested, g_failed, g_pending);
	return g_failed ? 1 : 0;
}
