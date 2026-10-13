#pragma once

// Batched-frame interpreter, clock accurate within a frame (doc/esp/README.md). The
// host is not serviced inside a frame, so the configuration is hoisted and empty
// slots are skipped.

#include "esp_base.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace esp
{
	struct DintEdge { uint16_t cycle; uint8_t before, after; };

	template <int lg2eramSize>
	class Interpreter : public Base<lg2eramSize>
	{
	public:
		using B = Base<lg2eramSize>;
		using RuntimeT = typename B::RuntimeT;
		using B::runtime;
		using B::instr;
		using B::words;
		using B::configuration;
		using B::hostTailAddress;
		using B::conditionalBranches;
		using B::iramInhibit;
		using B::receiveInput;
		using B::onDint;
		using B::dintFrameCycles;
		using B::irq0SelectCore1;
		using B::irq1SelectCore1;

		explicit Interpreter(RuntimeT* shared = nullptr) : B(shared) {}

		// Run 'clocks' DSP clocks of the current frame. A board calls this once
		// per sample (768, or 384 on the JP boards) and then sync().
		void runProgram(uint32_t clocks = FRAME_CLOCKS)
		{
			RuntimeT& rt = runtime();
			if (m_nopRunsGeneration != this->structureGeneration()) refreshNopRuns();
			const uint32_t end = rt.frameCycle + clocks;
			// Frame constants: nothing below can change until the next host write,
			// and the host only writes between frames.
			if (rt.frameCycle == 0) rt.framePaused = (configuration[2] & 0x40) != 0;
			const bool clock = this->dspClockRunning();
			const bool run0 = this->coreRunning(0), run1 = this->coreRunning(1);
			const auto [fall, rise] = pulseEdges(clock);
			const bool skippable = clock && !receiveInput && !iramInhibit;
			// An inhibited core's IRAM reads zero. Host and board writes land between frames, so it is
			// cleared once here, and the core's stores go to a scratch ring for the rest of the call.
			for (unsigned c = 0; c < 2; ++c)
			{
				const bool inhibit = iramInhibit & (1 << c);
				if (inhibit) std::memset(rt.core[c].iram, 0, sizeof(rt.core[c].iram));
				m_iramStore[c] = inhibit ? m_iramScratch[c] : rt.core[c].iram;
			}
			if (m_inhibitFastPath && iramInhibit == 3 && clock && run0 && run1 && !receiveInput && rt.frameCycle == 0
				&& !m_hasBranch[0] && !m_hasBranch[1] && rt.core[0].pc == 0 && rt.core[1].pc == 0
				&& rt.core[0].pcjumpat < 0 && rt.core[1].pcjumpat < 0)
			{
				runInhibitedFrame(clocks);
				return;
			}

			while (rt.frameCycle < end)
			{
				const uint32_t cycle = rt.frameCycle;
				// Cheap probe first: both running cores sit on an all-zero word.
				if (skippable && cycle < NORMAL_DECODE_END
					&& (!run0 || m_nopRuns[0][rt.core[0].pc]) && (!run1 || m_nopRuns[1][rt.core[1].pc]))
				{
					const uint32_t k = emptyRun(cycle, end, run0, run1, fall, rise);
					if (k)
					{
						fastForward(0, k, run0);
						fastForward(1, k, run1);
						if (lg2eramSize) rt.eram.cycle += k;
						rt.frameCycle += k;
						// The pulse level can only change at its two boundaries.
						if (rt.frameCycle == fall || rt.frameCycle == rise) publishPulse(fall, rise);
						continue;
					}
				}
				if (clock)
				{
					if (receiveInput) receiveInputs(rt.frameCycle);
					stepClock(run0, run1);
				}
				++rt.frameCycle;
				if (rt.frameCycle == fall || rt.frameCycle == rise) publishPulse(fall, rise);
			}
		}

		// The native kernel's scheduling contract: core 1 owns ERAM and records
		// GRAM requests; core 0 replays the corresponding arbitration stalls.
		// Runs while a kernel compiles, so native and interpreted frames agree.
		void runSequential(uint32_t clocks)
		{
			auto& rt = runtime();
			if (clocks > FRAME_CLOCKS || rt.frameCycle || !this->dspClockRunning() || receiveInput || iramInhibit
				|| (configuration[2] & 0x40) || !this->coreRunning(0) || !this->coreRunning(1))
			{
				runProgram(clocks);
				return;
			}
			rt.framePaused = false;
			bool gramRequests[FRAME_CLOCKS] = {};
			for (unsigned c : {1u, 0u})
			{
				auto& core = rt.core[c];
				m_iramStore[c] = core.iram;
				for (uint32_t cycle = 0; cycle < clocks; ++cycle)
				{
					rt.frameCycle = cycle;
					const bool normal = cycle < NORMAL_DECODE_END;
					const Instr* next = core.pc < PRAM_SIZE
						? &instr[c][int32_t(core.pc) == core.pcjumpat ? core.pcjumpto : core.pc] : nullptr;
					if (normal && next)
					{
						if (c == 1 && next->requestsGram) gramRequests[cycle] = true;
						if (next->coefWriteMask & (1 << c))
							rt.mulcoeffs[next->coefIndex] = (next->coefFromB ? core.accB : core.accA).pipelineSat24();
					}
					if (lg2eramSize && c == 1)
						rt.eram.clock(normal && core.pc < PRAM_SIZE ? instr[1][core.pc].field : 0, cycle < 755, cycle != 757);
					if (c == 0 && normal && next && next->requestsGram && gramRequests[cycle])
						stallGram(0);
					else executeCore(c, true, normal, next);
				}
			}
			rt.frameCycle = 0;
			finishBatchedFrame(clocks);
		}

		// Frame boundary: IRQ1 history, program counters, circular memories.
		void sync()
		{
			RuntimeT& rt = runtime();
			if (this->dspClockRunning())
			{
				const bool sign = irq1SelectCore1 ? rt.core[1].irq1Sign : rt.core[0].irq1Sign;
				rt.irq1Request = sign != rt.irq1History;
				rt.irq1History = sign;
				if (receiveInput) rt.writeGram(se<24>(receiveInput(0x60)), 0x5f);
				const bool advanceMemory = !(configuration[1] & 0x40);
				rt.syncCores(advanceMemory);
				if (lg2eramSize && advanceMemory) rt.eram.tickSample();
			}
			publishDint(uint8_t((irq0SelectCore1 ? rt.core[1].dintPin : rt.core[0].dintPin) | 2));
			rt.frameCycle = 0;
		}

		// After an engine ran a whole frame without the per-clock loop: advance
		// the frame envelope (IRQ1 pulse edges at their cycles, frame cycle).
		void finishBatchedFrame(uint32_t clocks)
		{
			RuntimeT& rt = runtime();
			const auto [fall, rise] = pulseEdges(this->dspClockRunning());
			for (const uint32_t edge : {fall, rise})
				if (edge > rt.frameCycle && edge <= clocks) { rt.frameCycle = edge; publishPulse(fall, rise); }
			rt.frameCycle = clocks;
		}

		uint8_t readDint() const { return runtime().publishedDint; }
		// Frames with both cores' outputs cleared run their stores only (runInhibitedFrame); off runs
		// them through the full interpreter.
		void setInhibitFastPath(bool enable) { m_inhibitFastPath = enable; }
		uint64_t inhibitedFrames() const { return m_inhibitedFrames; }
		// IRQ pin transitions since the last drain, in order, with the DSP clock
		// they happened at. A board replays them into its MCU after the frame.
		unsigned dintEdgeCount() const { return m_edgeCount; }
		const DintEdge& dintEdge(unsigned i) const { return m_edges[i]; }
		void drainDintEdges() { m_edgeCount = 0; }

	private:
		DintEdge m_edges[8] = {};
		unsigned m_edgeCount = 0;
		// Length of the all-zero word run starting at each PC, per core.
		uint16_t m_nopRuns[2][PRAM_WORDS] = {};
		// Where step() stores IRAM words: the core's IRAM, or while it is inhibited a scratch ring
		// nothing reads (set per runProgram call).
		int32_t* m_iramStore[2] = {};
		int32_t m_iramScratch[2][RING_SIZE] = {};
		uint64_t m_nopRunsGeneration = ~0ull;
		// One zero export of an output-cleared frame (buildInhibitedFrame).
		enum class InhibitKind : uint8_t { Gram, Coefficient, Dint, Irq1, VarOffset, Mailbox, WriteLatch };
		struct InhibitEvent { uint16_t cycle; InhibitKind kind; uint8_t core, arg; };
		bool m_hasBranch[2] = {};	// a branch in the executable words (the inhibited fast path does not follow them)
		bool m_inhibitFastPath = true;
		uint64_t m_inhibitedFrames = 0;
		std::vector<InhibitEvent> m_inhibitEvents;
		uint32_t m_inhibitPc[2] = {}, m_inhibitClocks = 0;
		uint64_t m_inhibitGeneration = ~0ull;

		void refreshNopRuns()
		{
			for (unsigned c = 0; c < 2; ++c)
			{
				uint16_t run = 0;
				m_hasBranch[c] = false;
				for (int pc = PRAM_WORDS - 1; pc >= 0; --pc)
				{
					const Instr& in = instr[c][pc];
					run = in.op == Op::Nop ? uint16_t(run + 1) : 0;
					m_nopRuns[c][pc] = run;
					if (pc < int(PRAM_SIZE) && isBranch(in)) m_hasBranch[c] = true;
				}
			}
			m_nopRunsGeneration = this->structureGeneration();
		}

		// A frame with both cores' outputs cleared (2005 bits 5/6 = 60): every export is zero and IRAM
		// reads zero, so what the frame leaves behind is its zero stores into GRAM, the coefficient
		// registers, the mailbox, the IRQ latches and the ERAM write latch, plus the ERAM controller
		// writing that latch. Those run clock by clock in stepClock's order, GRAM stalls included; the
		// arithmetic is skipped and the accumulators and predicates are left at zero. Firmware runs whole
		// programs this way while it rewrites them, some to clear their delay memory.
		void runInhibitedFrame(uint32_t clocks)
		{
			RuntimeT& rt = runtime();
			if (m_inhibitGeneration != this->structureGeneration() || m_inhibitClocks != clocks) buildInhibitedFrame(clocks);
			rt.framePaused = (configuration[2] & 0x40) != 0;
			for (auto& core : rt.core)
			{
				core.accA.acc = core.accB.acc = 0;
				std::memset(core.accA.hist, 0, sizeof(core.accA.hist));
				std::memset(core.accB.hist, 0, sizeof(core.accB.hist));
				core.skipfieldPos = core.skipfieldNeg = 0;
				core.predicateReady = core.predicateValues = 0;
				core.comparePending = core.compareNegative = false;
				core.lastMulInputA = 0;
				core.lastMul30 = false;
			}
			// The controller clocks every cycle, reading core1's field live (a field is not structure);
			// an idle controller only counts. The zero exports land after it in their cycle, as in
			// stepClock, where the ERAM tick precedes both cores.
			const InhibitEvent* event = m_inhibitEvents.data();
			const InhibitEvent* const eventsEnd = event + m_inhibitEvents.size();
			const uint32_t fieldEnd = std::min(clocks, std::min(NORMAL_DECODE_END, PRAM_SIZE));
			for (uint32_t cycle = 0; cycle < clocks; )
			{
				if (lg2eramSize)
				{
					auto& eram = rt.eram;
					if (!eram.active() && !eram.deferredCommand)
					{
						// Idle: count up to the next field or event.
						const uint32_t limit = event != eventsEnd ? std::min<uint32_t>(event->cycle, clocks) : clocks;
						uint32_t next = cycle;
						while (next < limit && (next >= fieldEnd || !instr[1][next].field)) ++next;
						eram.cycle += next - cycle;
						cycle = next;
						if (cycle >= clocks) break;
					}
					eram.clock(cycle < fieldEnd ? instr[1][cycle].field : 0, cycle < 755, cycle != 757);
				}
				else if (event != eventsEnd && event->cycle > cycle)
				{
					cycle = std::min<uint32_t>(event->cycle, clocks);
					continue;
				}
				for (; event != eventsEnd && event->cycle == cycle; ++event) applyInhibitEvent(*event);
				++cycle;
			}
			rt.core[0].pc = m_inhibitPc[0];
			rt.core[1].pc = m_inhibitPc[1];
			++m_inhibitedFrames;
			finishBatchedFrame(clocks);
		}

		// What an output-cleared frame of this program does, by cycle: its zero exports in the order
		// stepClock performs them (core0, then core1), with core0 waiting on GRAM collisions, and the
		// PCs it ends on.
		void buildInhibitedFrame(uint32_t clocks)
		{
			m_inhibitEvents.clear();
			uint32_t pc[2] = {0, 0};
			for (uint32_t cycle = 0; cycle < clocks; ++cycle)
			{
				const bool normal = cycle < NORMAL_DECODE_END;
				const Instr* next[2] = {normal && pc[0] < PRAM_SIZE ? &instr[0][pc[0]] : nullptr,
					normal && pc[1] < PRAM_SIZE ? &instr[1][pc[1]] : nullptr};
				const bool stall0 = next[0] && next[1] && next[0]->requestsGram && next[1]->requestsGram;
				const auto add = [&](InhibitKind kind, unsigned core, unsigned arg) { m_inhibitEvents.push_back({uint16_t(cycle), kind, uint8_t(core), uint8_t(arg)}); };
				for (unsigned c = 0; c < 2; ++c)
					if (next[c] && (next[c]->coefWriteMask & (1 << c))) add(InhibitKind::Coefficient, c, next[c]->coefIndex);
				for (unsigned c = 0; c < 2; ++c)
				{
					if (c == 0 && stall0) continue;
					if (next[c])
					{
						const Instr& in = *next[c];
						if (in.op == Op::GramStoreReplace || in.op == Op::GramStoreAdd) add(InhibitKind::Gram, c, in.mem);
						else if (in.control)
						{
							switch (in.sub)
							{
							case 0x4: add(InhibitKind::Dint, c, 0); break;
							case 0x5: add(InhibitKind::Irq1, c, 0); break;
							case 0x7: if (c == 1) add(InhibitKind::VarOffset, c, 0); break;
							case 0xa: add(InhibitKind::Mailbox, c, 0); break;
							case 0xb: if (c == 1) add(InhibitKind::WriteLatch, c, 0); break;
							default: break;
							}
						}
					}
					if (pc[c] < PRAM_SIZE && (next[c] || !normal)) ++pc[c];
				}
			}
			m_inhibitPc[0] = pc[0];
			m_inhibitPc[1] = pc[1];
			m_inhibitClocks = clocks;
			m_inhibitGeneration = this->structureGeneration();
		}

		void applyInhibitEvent(const InhibitEvent& e)
		{
			RuntimeT& rt = runtime();
			auto& core = rt.core[e.core];
			switch (e.kind)
			{
			case InhibitKind::Gram: rt.gram[core.ringIndex(e.arg)] = 0; break;
			case InhibitKind::Coefficient: rt.mulcoeffs[e.arg] = 0; break;
			case InhibitKind::Dint: core.dintPin = 0; break;
			case InhibitKind::Irq1: core.irq1Sign = false; break;
			case InhibitKind::VarOffset: rt.eram.varOffset = 0; break;
			case InhibitKind::Mailbox: std::memset(rt.mailbox, 0, sizeof(rt.mailbox)); break;
			case InhibitKind::WriteLatch: rt.eram.writeLatch = 0; break;
			}
		}

		// How many clocks from 'cycle' both cores would spend on all-zero words
		// with nothing else pending: no branch redirect, idle predicates, idle
		// ERAM controller, no IRQ edge, inside normal decode and this call.
		uint32_t emptyRun(uint32_t cycle, uint32_t end, bool run0, bool run1, uint32_t fall, uint32_t rise) const
		{
			const RuntimeT& rt = runtime();
			if (lg2eramSize && (rt.eram.active() || rt.eram.deferredCommand)) return 0;
			uint32_t k = std::min(end, NORMAL_DECODE_END) - cycle;
			if (cycle < fall) k = std::min(k, fall - cycle);
			else if (cycle < rise) k = std::min(k, rise - cycle);
			for (unsigned c = 0; c < 2; ++c)
			{
				const auto& core = rt.core[c];
				if (!(c ? run1 : run0)) continue;	// a halted core only advances its PC
				if (core.pc >= PRAM_SIZE) { k = 0; break; }
				if (!core.predicateIdle()) { k = 0; break; }
				// A core that ran off the end sits at PRAM_SIZE without clocking
				// its pipeline: the skip may bring it there, never beyond.
				k = std::min(k, PRAM_SIZE - core.pc);
				if (core.pcjumpat >= 0)
				{
					if (core.pcjumpat <= int32_t(core.pc)) { k = 0; break; }
					k = std::min(k, uint32_t(core.pcjumpat) - core.pc);
				}
				k = std::min<uint32_t>(k, m_nopRuns[c][core.pc]);
			}
			return k;
		}

		void fastForward(unsigned c, uint32_t k, bool run)
		{
			auto& core = runtime().core[c];
			if (!run) { core.pc = std::min<uint32_t>(core.pc + k, PRAM_SIZE); return; }
			core.pc += k;
			// k NOP finishCondition() calls with idle predicates; the fields are 16 bits wide.
			if (k >= 16) core.skipfieldPos = core.skipfieldNeg = 0;
			else { core.skipfieldPos >>= k; core.skipfieldNeg >>= k; }
			core.lastMul30 = false;
			if (k >= 3) { core.accA.fastForward(k); core.accB.fastForward(k); }
			else for (uint32_t i = 0; i < k; ++i) { core.accA.storePipeline(); core.accB.storePipeline(); }
		}

		// IRQ1 pulse window in frame cycles, low on [fall, rise): a function of
		// the clock source and the frame length only, so it is cached.
		std::pair<uint32_t, uint32_t> pulseEdges(bool clock)
		{
			const uint64_t key = (uint64_t(dintFrameCycles) << 1) | (clock ? 1 : 0);
			if (key != m_pulseKey)
			{
				const unsigned frameClocks = clock ? 768 : 1024, begin = clock ? 38 : 294;
				m_pulseFall = uint32_t((uint64_t(begin) * dintFrameCycles + frameClocks - 1) / frameClocks);
				m_pulseRise = uint32_t((uint64_t(begin + 512) * dintFrameCycles + frameClocks - 1) / frameClocks);
				m_pulseKey = key;
			}
			return {m_pulseFall, m_pulseRise};
		}
		uint64_t m_pulseKey = ~0ull;
		uint32_t m_pulseFall = 0, m_pulseRise = 0;

		void publishPulse(uint32_t fall, uint32_t rise)
		{
			RuntimeT& rt = runtime();
			const bool low = rt.irq1Request && rt.frameCycle >= fall && rt.frameCycle < rise;
			publishDint(uint8_t((rt.publishedDint & 1) | (low ? 0 : 2)));
		}

		void publishDint(uint8_t value)
		{
			RuntimeT& rt = runtime();
			const uint8_t previous = rt.publishedDint;
			if (previous == value) return;
			rt.publishedDint = value;
			if (m_edgeCount < 8) m_edges[m_edgeCount++] = {uint16_t(rt.frameCycle), previous, value};
			if (onDint) onDint(rt.frameCycle, previous, value);
		}
		void receiveWord(uint8_t operand) { runtime().writeGram(se<24>(receiveInput(operand)), operand); }

		void receiveInputs(uint32_t cycle)
		{
			if (!(configuration[2] & 0x10) && cycle >= 2 && cycle <= 754 && (cycle - 2) % 16 == 0) receiveWord(uint8_t((cycle - 2) / 8));
			if (cycle == 10 || cycle == 26 || cycle == 42) receiveWord(uint8_t(0x62 + (cycle - 10) / 8));
			if (cycle >= 378 && cycle <= 426 && (cycle - 378) % 16 == 0) receiveWord(uint8_t(0x68 + (cycle - 378) / 8));
		}

		// One DSP clock with the clock running.
		void stepClock(bool run0, bool run1)
		{
			RuntimeT& rt = runtime();
			const uint32_t cycle = rt.frameCycle;
			const bool normal = cycle < NORMAL_DECODE_END;
			auto& c0 = rt.core[0];
			auto& c1 = rt.core[1];
			// The instruction each running core is about to execute, with a
			// pending branch redirect applied. Used for arbitration, coefficient
			// publication and execution alike.
			const Instr* next0 = run0 && c0.pc < PRAM_SIZE ? &instr[0][int32_t(c0.pc) == c0.pcjumpat ? c0.pcjumpto : c0.pc] : nullptr;
			const Instr* next1 = run1 && c1.pc < PRAM_SIZE ? &instr[1][int32_t(c1.pc) == c1.pcjumpat ? c1.pcjumpto : c1.pc] : nullptr;
			const bool stall0 = normal && next0 && next1 && next0->requestsGram && next1->requestsGram;
			if (normal)
			{
				// Both cores can consume a coefficient published in this cycle.
				if (next0 && (next0->coefWriteMask & 1)) rt.mulcoeffs[next0->coefIndex] = (next0->coefFromB ? c0.accB : c0.accA).pipelineSat24();
				if (next1 && (next1->coefWriteMask & 2)) rt.mulcoeffs[next1->coefIndex] = (next1->coefFromB ? c1.accB : c1.accA).pipelineSat24();
			}
			if (lg2eramSize)
			{
				// The controller samples the field at core1's PC before any branch redirect.
				rt.eram.clock(run1 && normal && c1.pc < PRAM_SIZE ? instr[1][c1.pc].field : 0, run1 && cycle < 755, cycle != 757);
			}
			// core0, then core1: shared exports are ordered core0 then core1.
			if (stall0) stallGram(0); else executeCore(0, run0, normal, next0);
			executeCore(1, run1, normal, next1);
		}

		ESP_ALWAYS_INLINE void executeCore(unsigned c, bool run, bool normal, const Instr* next)
		{
			auto& core = runtime().core[c];
			if (!run) { if (core.pc < PRAM_SIZE) ++core.pc; return; }
			if (normal) { if (next) step(c, *next); }
			else stepTail(c, runtime().frameCycle - NORMAL_DECODE_END, !(configuration[1] & 0x40));
		}

		void stallGram(unsigned c)
		{
			auto& core = runtime().core[c];
			// The instruction waits, but the delay lines keep clocking and a
			// pending branch ages with them.
			if (core.pcjumpat > int32_t(core.pc) + 1) --core.pcjumpat;
			idleClock(core);
		}

		// A clock without arithmetic: the predicate and accumulator pipelines advance.
		static ESP_ALWAYS_INLINE void idleClock(CoreRuntime& core)
		{
			finishCondition(core, false);
			core.lastMul30 = false;
			core.accA.storePipeline();
			core.accB.storePipeline();
		}

		int32_t coefficientValue(uint8_t index, uint8_t transform) const
		{
			const RuntimeT& rt = runtime();
			int32_t value = rt.mulcoeffs[index == 7 ? 5 : index];
			if (index == 6) value = int32_t((uint32_t(rt.eram.varOffset) << 11) & 0x7fffff);
			if (transform == 8) value = ~value;
			if (transform == 16) value = value >= 0 ? (~value & 0x7fffff) : ~(value & 0x7fffff);
			return value;
		}

		// CMP: A's sign is sampled after the following instruction; the
		// predicate reaches the multiplicand selector at +5, suppressing +5/+6
		// when nonnegative and +7..+10 when negative.
		static void finishCondition(CoreRuntime& core, bool newCompare)
		{
			// Idle pipeline: every shift below is of zeros (predicate values only exist with their
			// ready bits) and comparePending stays false.
			if (!newCompare && !core.comparePending && !(core.skipfieldPos | core.skipfieldNeg | core.predicateReady)) return;
			core.skipfieldPos >>= 1;
			core.skipfieldNeg >>= 1;
			if (core.predicateReady & 1) core.compareNegative = (core.predicateValues & 1) != 0;
			core.predicateReady >>= 1;
			core.predicateValues >>= 1;
			if (core.comparePending)
			{
				core.predicateReady |= 4;
				if (core.accA.rawFull() < 0) core.predicateValues |= 4;
				core.skipfieldPos |= 0x18;
				core.skipfieldNeg |= 0x1e0;
			}
			core.comparePending = newCompare;
		}

		static void jumpto(CoreRuntime& core, uint16_t target) { core.pcjumpto = target; core.pcjumpat = int32_t(core.pc) + 2; }

		// Execute 'in', the instruction at the (branch-resolved) PC of core c.
		ESP_ALWAYS_INLINE void step(unsigned c, const Instr& in)
		{
			RuntimeT& rt = runtime();
			auto& core = rt.core[c];
			if (int32_t(core.pc) == core.pcjumpat) { core.pc = uint32_t(core.pcjumpto); core.pcjumpat = -1; }
			++core.pc;
			if (in.op == Op::Nop)
			{
				idleClock(core);
				return;
			}
			const uint32_t mempos = core.ringIndex(in.mem);
			int32_t* const store = m_iramStore[c];
			int32_t mulA = in.useConstant ? in.constant : core.iram[mempos];
			int32_t mulB = int32_t(uint8_t(in.coef));
			const uint8_t shift = in.shift;
			const bool accB = in.accB;
			const bool replace = in.replace;
			bool setCondition = false;
			switch (in.op)
			{
			case Op::Nop: case Op::Mac: case Op::Op54: break;
			case Op::StoreSat: store[mempos] = mulA = (in.storeB ? core.accB : core.accA).pipelineSat24(); break;
			case Op::GramRead: mulA = rt.gram[mempos]; break;
			case Op::GramStoreReplace: rt.gram[mempos] = (in.storeB ? core.accB : core.accA).pipelineSat24(); break;
			case Op::GramStoreAdd: rt.gram[mempos] = mulA = (in.storeB ? core.accB : core.accA).pipelineSat24(); break;
			case Op::Mul30:
			{
				const uint8_t flags = uint8_t(in.coef);
				const bool weird = (flags & 0x1c) == 0x1c;
				if (flags & 4)
				{
					mulA = (accB ? core.accB : core.accA).pipelineSat24();
					if (weird) mulA = mulA >= 0 ? 0x7fffff : -0x800000;
					store[mempos] = mulA;
				}
				core.lastCoefficientTransform = flags & 0x18;
				mulB = coefficientValue(flags >> 5, core.lastCoefficientTransform) >> 16;
				break;
			}
			case Op::Special:
				if (in.control)
				{
					Accumulator& ac = accB ? core.accB : core.accA;
					const uint16_t target = uint16_t(in.word & (conditionalBranches ? 0x3ff : 0xff));
					switch (in.sub)
					{
					case 0x0: case 0x1: case 0x2:
					{
						const int32_t v = ac.pipelineRawFull();
						const bool taken = !conditionalBranches || (in.sub == 0 ? v == 0 : in.sub == 1 ? v < 0 : v >= 0);
						if (taken) jumpto(core, target);
						break;
					}
					case 0x3: jumpto(core, target); break;
					case 0x4: core.dintPin = ac.pipelineOutputFull() < 0 ? 1 : 0; break;
					case 0x5: core.irq1Sign = ac.pipelineOutputFull() < 0; break;
					case 0x6:
						mulA = core.lastMulInputA >> 7;
						if (core.lastMul30) mulB = (coefficientValue(uint8_t(in.coef) >> 5, core.lastCoefficientTransform) >> 9) & 0x7f;
						break;
					case 0x7: if (c == 1) rt.eram.varOffset = ac.pipelineRawFull(); break;
					case 0xa: { const int32_t v = ac.pipelineSat24(); std::memcpy(rt.mailbox, &v, 4); break; }
					case 0xb: if (c == 1) rt.eram.writeLatch = ac.pipelineSat24(); break;
					case 0xc: case 0xd: case 0xe: case 0xf:
						mulA = c == 1 ? rt.eram.readLatch : 0;
						store[core.ringIndex(in.mem | 0xf0)] = mulA;
						break;
					default: break;
					}
				}
				break;
			case Op::StoreRaw: store[mempos] = mulA = core.accA.pipelineRaw24(); break;
			case Op::StoreRect: store[mempos] = mulA = std::max(0, core.accA.pipelineSat24()); break;
			case Op::Cmp: setCondition = true; break;
			case Op::Invert: mulA = (~mulA) & 0x7fffff; break;
			case Op::StoreFoldPos:
				store[mempos] = mulA = core.accA.pipelineSat24();
				if (mulA >= 0) mulA = ~mulA;
				mulA &= 0x7fffff;
				break;
			case Op::StoreFoldNeg:
				store[mempos] = mulA = core.accA.pipelineSat24();
				if (mulA < 0) mulA = ~mulA;
				mulA &= 0x7fffff;
				break;
			}
			if ((core.compareNegative ? core.skipfieldNeg : core.skipfieldPos) & 1) mulA = 0;

			Accumulator& dest = accB ? core.accB : core.accA;
			if (replace) dest.set(0);
			// signed24 x signed8 fits in 32 bits (|product| <= 2^30).
			dest.add((se<24>(mulA) * se<8>(mulB)) >> shift);

			finishCondition(core, setCondition);
			core.lastMulInputA = mulA;
			core.lastMul30 = in.op == Op::Mul30;
			core.accA.storePipeline();
			core.accB.storePipeline();
		}

		// Slots 756/757 multiply raw IRAM at relative 00/FF (00/00 when frozen)
		// into A; 758..767 multiply the retained source by the five words at the
		// last committed host address, twice each.
		void stepTail(unsigned c, uint32_t tailCycle, bool advanceMemory)
		{
			auto& core = runtime().core[c];
			if (core.pc < PRAM_SIZE)
			{
				const uint32_t word = words[c][core.pc++];
				if (tailCycle < 2)
				{
					const uint8_t mem = tailCycle == 0 || !advanceMemory ? 0 : 0xff;
					core.lastMulInputA = se<24>(core.readIram(mem));
					core.accA.add(int32_t((int64_t(core.lastMulInputA) * se<8>(int32_t(word & 255))) >> shiftAmount((word >> 8) & 3)));
				}
				else if (tailCycle < 12 && hostTailAddress[c] >= 0)
				{
					const uint32_t address = uint32_t(hostTailAddress[c]) + (tailCycle - 2) / 2;
					const uint32_t hostWord = words[c][hostTailAddress[c] < int32_t(PRAM_SIZE) ? std::min(address, PRAM_SIZE - 1) : address & 0x3ff];
					core.accA.add(int32_t((int64_t(core.lastMulInputA) * se<8>(int32_t(hostWord & 255))) >> shiftAmount((hostWord >> 8) & 3)));
				}
			}
			idleClock(core);
		}
	};
}
