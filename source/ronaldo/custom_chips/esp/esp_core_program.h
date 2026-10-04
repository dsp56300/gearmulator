#pragma once

#include "esp_program.h"

#include <array>
#include <bitset>
#include <map>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace esp
{
	// Optimizer IR: basic blocks carry resolved branch-delay, ERAM transaction and
	// CMP timing. Context is part of a block label, never an emitted controller loop.
	struct CoreProgram
	{
		// Timing/CMP contexts can multiply on adversarial branch graphs. Bound
		// compilation before allocating native IR; the dispatcher caches refusal
		// and keeps the sequential interpreter, without changing frame semantics.
		static constexpr unsigned maxNodes = 8192;
		struct CompileLimit : std::runtime_error
		{
			CompileLimit() : std::runtime_error("ESP control-flow graph exceeds compile limit") {}
		};
		struct Predicate
		{
			unsigned pos = 0, neg = 0, nextSlot = 0;
			int pending = -1, govern = -1;
			std::array<int, 3> ready = {{-1, -1, -1}};
			auto key() const { return std::tie(pos, neg, nextSlot, pending, govern, ready); }
			bool operator<(const Predicate& b) const { return key() < b.key(); }
			bool operator==(const Predicate& b) const { return key() == b.key(); }
		};
		struct Controller
		{
			int next = -1, current = -1, cooldown = 0, previousAge = -1;
			unsigned nextMode = 0, currentMode = 0, deferred = 0;
			bool shortNext = false, shortCurrent = false;
			bool immediateKnown = false;
			std::array<int, 4> immediatePc = {{-1, -1, -1, -1}};
			unsigned variableMask = 0;
			int immediateTable = -1;
			unsigned immediateMask = 0;
			auto key() const
			{
				return std::tie(next, current, cooldown, previousAge, nextMode, currentMode, deferred, shortNext,
								shortCurrent, immediateKnown, immediatePc, variableMask);
			}
			bool operator<(const Controller& b) const { return key() < b.key(); }
		};
		struct EramStep
		{
			Controller before, after;
			unsigned field = 0, collect = 0, capture = 0, captureAfter = 0;
			// Observable metadata; the controller never reads these at runtime.
			enum Flag
			{
				ActiveNext = 1,
				ActiveCurrent = 2,
				ShortNext = 4,
				ShortCurrent = 8
			};
			unsigned publishFlags = 0;
			int earlyHold = -1;
			bool start = false, defer = false, low = false, commit = false, address = false;
			bool restart = false, restartDeferred = false;
			unsigned extendedMask = 0, addressExtendedMask = 0;
			bool extendedFinish = false;
			bool transfer = true;
			bool saveWriteSample = true, saveWritePacked = true;
			enum ReadStage
			{
				Address = 1,
				Low = 2,
				Commit = 4
			};
			unsigned readLatchStages = Address | Low | Commit;
			bool empty() const
			{
				return !(start || capture || captureAfter || defer || low || commit || address || extendedMask ||
						 extendedFinish);
			}
		};
		struct State
		{
			unsigned pc = 0, head = 0, tick = 0;
			// Unknown only at frame entry; branch joins retain distinct contexts.
			int lastMul30 = -1, lastTransform = -1;
			int at = -1;
			unsigned target = 0;
			bool commandCutoff = false;
			Controller controller;
			Predicate predicate;
			bool operator<(const State& b) const
			{
				return std::tie(pc, head, tick, lastMul30, lastTransform, at, target, commandCutoff, controller,
								predicate) < std::tie(b.pc, b.head, b.tick, b.lastMul30, b.lastTransform, b.at,
													  b.target, b.commandCutoff, b.controller, b.predicate);
			}
		};
		struct TailStep
		{
			State before, after;
			EramStep eram;
			unsigned clock = 0;
		};
		struct Node
		{
			State state;
			unsigned pc = 0;
			int next = -1, taken = -1, stall = -1;
			bool branch = false, conditional = false, idle = false;
			Predicate predicate, predicateAfter;
			int sample = -1;
			unsigned liveIn = 0, liveOut = 0;
			bool deadMac = false, keepInput = true;
			EramStep eram;
			std::vector<TailStep> tail;
			std::bitset<FRAME_CLOCKS + 1> times;
		};
		struct Operation
		{
			unsigned node = 0, pipe[2] = {}, current[2] = {}, destination = 0;
			bool dead = false, keepInput = true, operand24 = false;
			int constantShift = -1; // log2 of the unsuppressed ordinary MAC operand
		};
		struct Block
		{
			std::vector<Operation> ops;
			unsigned history[2][3] = {}, current[2] = {};
			unsigned liveOut = 0;
		};
		unsigned core = 0, clocks = 0;
		bool hasCmp = false, needsClock = false;
		std::vector<Node> nodes;
		std::vector<Block> blocks;
		std::vector<unsigned> owner;
		std::bitset<FRAME_CLOCKS + 1> gramCycles;

		static Predicate advancePredicate(Predicate p, bool compare)
		{
			p.pos >>= 1;
			p.neg >>= 1;
			if (p.ready[0] >= 0)
				p.govern = p.ready[0];
			p.ready[0] = p.ready[1];
			p.ready[1] = p.ready[2];
			p.ready[2] = -1;
			if (p.pending >= 0)
			{
				p.ready[2] = p.pending;
				p.pos |= 0x18;
				p.neg |= 0x1e0;
			}
			p.pending = compare ? int(p.nextSlot) : -1;
			if (compare)
				p.nextSlot = (p.nextSlot + 1) & 7;
			return p;
		}
		static EramStep lowerEram(const ProgramSnapshot& p, Controller s, unsigned field, bool accept,
								  bool transfer = true, int pc = -1)
		{
			EramStep e;
			e.before = s;
			e.field = field;
			e.transfer = transfer;
			accept &= s.cooldown == 0;
			if (accept && s.next < 0 && field)
			{
				e.start = true;
				s.next = 0;
				s.nextMode = field;
				s.shortNext = false;
				s.cooldown = int(p.spacing);
				s.immediateKnown = true;
				s.immediatePc.fill(-1);
				s.variableMask = 0;
			}
			else if (s.next >= 1 && s.next <= 4)
			{
				e.collect = unsigned(s.next);
				s.immediatePc[s.next - 1] = pc;
				s.variableMask |= (31u << ((s.next - 1) * 5)) & 0x3ffff;
			}
			if (s.next == 4 && accept && field)
			{
				e.defer = true;
				s.deferred = field;
			}
			const bool extended = p.extended();
			auto mask = [&](unsigned stage, bool write)
			{
				if (p.staticBus)
				{
					if (stage == 6)
						return p.rawSingle() ? 0xffffffu : p.compress ? 0xffu : 0xfffu;
					if (stage == 10 && !p.rawSingle())
						return p.compress ? 0xff00u : 0xfff000u;
					return 0u;
				}
				const unsigned phase = stage - unsigned(write);
				return phase == 5 ? 15u : phase == 6 ? 0xf0u : phase == 8 ? 0xf00u : phase == 9 ? 0xf000u : 0u;
			};
			if (s.current >= 0)
			{
				if (extended)
				{
					const bool write = s.currentMode == 0x10;
					e.extendedMask = transfer ? mask(unsigned(s.current), write) : 0;
					const int finish = p.staticBus ? (p.rawSingle() ? 6 : 10) : write ? 10 : 9;
					if (s.current >= finish)
					{
						e.extendedFinish = true;
						s.current = -1;
					}
				}
				else if (s.current == int(p.rawSingle() ? ERAM_COMMIT_STAGE_RAW_SINGLE : ERAM_COMMIT_STAGE))
				{
					e.commit = true;
					s.current = -1;
				}
				else if (!p.rawSingle() && s.current == int(ERAM_COMMIT_STAGE) - 1)
					e.low = true;
			}
			if (s.next == 5)
			{
				e.address = true;
				e.earlyHold = s.previousAge < 0 ? -1 : int(s.previousAge - 5 <= int(ERAM_EARLY_READ_HOLD_GAP));
				s.previousAge = 5;
				e.capture = s.variableMask;
				s.variableMask = 0;
				s.current = 5;
				s.currentMode = s.nextMode;
				s.shortCurrent = s.shortNext;
				s.next = -1;
				if (extended)
					e.addressExtendedMask = transfer ? mask(5, s.currentMode == 0x10) : 0;
				else if (p.rawSingle() && s.currentMode != 0x10)
					s.current = -1;
				if (s.deferred || (accept && field))
				{
					e.restart = true;
					e.restartDeferred = s.deferred != 0;
					s.next = e.restartDeferred ? 1 : 0;
					s.nextMode = s.deferred ? s.deferred : field;
					s.immediatePc.fill(-1);
					s.variableMask = 0;
					if (e.restartDeferred)
					{
						s.immediatePc[0] = pc;
						s.variableMask = 31;
					}
					s.shortNext = true;
					s.cooldown = int(p.spacing) - s.next;
					s.deferred = 0;
				}
			}
			if (s.next >= 0)
				++s.next;
			else
			{
				s.nextMode = 0;
				s.shortNext = false;
			}
			if (s.current >= 0)
				++s.current;
			else
			{
				s.currentMode = 0;
				s.shortCurrent = false;
			}
			if (s.previousAge >= 0)
				s.previousAge = std::min(s.previousAge + 1, 18);
			s.cooldown = std::max(0, s.cooldown - 1);
			// Fold sequential command fields to the host-maintained immediate table.
			// Branch-spanning collections retain their explicit operand references.
			s.immediateTable = -1;
			s.immediateMask = 0;
			if (s.immediatePc[0] > 0)
			{
				bool sequential = true;
				unsigned count = 0;
				for (unsigned i = 0; i < 4; ++i)
				{
					if (s.immediatePc[i] < 0)
						continue;
					sequential &= count == i && s.immediatePc[i] == s.immediatePc[0] + int(i) &&
						s.immediatePc[i] < int(NORMAL_DECODE_END);
					++count;
				}
				if (sequential)
				{
					s.immediateTable = s.immediatePc[0] - 1;
					s.immediateMask = (1u << (5 * count)) - 1;
				}
			}
			e.publishFlags = (e.start ? EramStep::ActiveNext | EramStep::ShortNext : 0) |
				(e.commit || e.extendedFinish ? EramStep::ActiveCurrent : 0) |
				(e.address ? EramStep::ActiveNext | EramStep::ActiveCurrent | EramStep::ShortCurrent : 0) |
				(e.restart ? EramStep::ActiveNext | EramStep::ShortNext : 0);
			e.after = s;
			return e;
		}
		static CoreProgram build(const ProgramSnapshot& program, unsigned c, unsigned count,
								 const std::bitset<FRAME_CLOCKS + 1>& otherGram = {}, bool eram = true)
		{
			CoreProgram p;
			p.core = c;
			p.clocks = std::min(count, NORMAL_DECODE_END);
			bool commands = false;
			if (c == 1 && eram)
				for (unsigned pc = 0; pc < PRAM_SIZE; ++pc)
					commands |= (program.words[1][pc] & 0x0c000000u) != 0;
			const bool timed = commands && count > 754;
			std::map<State, unsigned> ids;
			auto intern = [&](State s)
			{
				if (s.at < 0)
					s.target = 0;
				auto found = ids.find(s);
				if (found != ids.end())
					return found->second;
				if (p.nodes.size() >= maxNodes)
					throw CompileLimit();
				const unsigned id = unsigned(p.nodes.size());
				ids.emplace(s, id);
				Node n;
				n.state = s;
				n.idle = s.pc >= PRAM_SIZE;
				n.pc = !n.idle && int(s.pc) == s.at ? s.target : s.pc;
				n.predicate = s.predicate;
				if (c == 1 && eram)
				{
					const unsigned field = s.pc < PRAM_SIZE ? program.instruction(1, s.pc).field & 0x18 : 0;
					n.eram = lowerEram(program, s.controller, field, !s.commandCutoff, true,
									   s.pc < PRAM_SIZE ? int(s.pc) : -1);
					if (!n.idle)
					{
						const auto& in = program.instruction(c, n.pc);
						if (in.control && in.sub == 7)
						{
							n.eram.captureAfter = n.eram.after.variableMask;
							n.eram.after.variableMask = 0;
						}
					}
				}
				if (!n.idle)
				{
					n.sample = s.predicate.pending;
					n.predicateAfter = advancePredicate(s.predicate, program.instruction(c, n.pc).op == Op::Cmp);
				}
				else
					n.predicateAfter = s.predicate;
				p.nodes.push_back(n);
				return id;
			};
			intern({});
			p.nodes[0].times.set(0);
			for (unsigned t = 0; t < p.clocks; ++t)
				for (unsigned id = 0; id < p.nodes.size(); ++id)
				{
					if (!p.nodes[id].times[t])
						continue;
					const Node n = p.nodes[id];
					auto send = [&](State s)
					{
						if (c == 1 && eram)
						{
							s.controller = n.eram.after;
							s.commandCutoff = timed && t + 1 >= 755;
							if (timed)
								s.tick = t + 1;
						}
						const unsigned dst = intern(s);
						p.nodes[dst].times.set(t + 1);
						return int(dst);
					};
					if (n.idle)
					{
						p.nodes[id].next = send(n.state);
						continue;
					}
					const auto& in = program.instruction(c, n.pc);
					p.hasCmp |= in.op == Op::Cmp;
					if (in.requestsGram)
						p.gramCycles.set(t);
					if (c == 0 && in.requestsGram && otherGram[t])
					{
						auto s = n.state;
						if (s.at > int(s.pc) + 1)
							--s.at;
						s.predicate = advancePredicate(s.predicate, false);
						s.lastMul30 = 0;
						s.head = (s.head + 1) % 3;
						p.nodes[id].stall = send(s);
					}
					State next = n.state;
					next.pc = n.pc + 1;
					next.lastMul30 = in.op == Op::Mul30;
					if (in.op == Op::Mul30)
						next.lastTransform = uint8_t(in.coef) & 0x18;
					next.head = (next.head + 1) % 3;
					next.predicate = n.predicateAfter;
					if (int(n.state.pc) == n.state.at)
						next.at = -1;
					const bool branch = isBranch(in),
							   conditional = branch && program.conditionalBranches && in.sub != 3;
					p.nodes[id].branch = branch;
					p.nodes[id].conditional = conditional;
					if (!branch || conditional)
						p.nodes[id].next = send(next);
					if (branch)
					{
						next.at = int(n.pc) + 3;
						next.target = in.word & (program.conditionalBranches ? 1023 : 255);
						p.nodes[id].taken = send(next);
					}
				}
			for (auto& n : p.nodes)
				if (n.times[p.clocks])
				{
					State state = n.state;
					for (unsigned clock = NORMAL_DECODE_END; clock < count; ++clock)
					{
						TailStep step;
						step.before = state;
						step.clock = clock;
						if (c == 1 && eram)
						{
							step.eram = lowerEram(program, state.controller, 0, false, clock != 757);
							state.controller = step.eram.after;
						}
						state.predicate = advancePredicate(state.predicate, false);
						state.lastMul30 = 0;
						state.head = (state.head + 1) % 3;
						if (state.pc < PRAM_SIZE)
							++state.pc;
						step.after = state;
						n.tail.push_back(step);
					}
				}
			p.analyzeValues(program);
			if (c == 1 && eram)
			{
				p.analyzeEramValues(program);
				p.analyzeReadLatch(program);
			}
			for (const auto& n : p.nodes)
				p.needsClock |= n.times[p.clocks] && n.times.count() > 1;
			std::vector<unsigned> incoming(p.nodes.size());
			for (const auto& n : p.nodes)
				for (int dst : {n.next, n.taken, n.stall})
					if (dst >= 0)
						++incoming[dst];
			p.owner.assign(p.nodes.size(), ~0u);
			for (unsigned id = 0; id < p.nodes.size(); ++id)
			{
				if (p.owner[id] != ~0u)
					continue;
				Block b;
				unsigned at = id;
				for (;;)
				{
					p.owner[at] = unsigned(p.blocks.size());
					Operation op;
					op.node = at;
					b.ops.push_back(op);
					const auto& n = p.nodes[at];
					if (n.idle || n.branch || n.stall >= 0 || n.next < 0)
						break;
					const unsigned next = unsigned(n.next);
					if (incoming[next] != 1 || p.owner[next] != ~0u || p.nodes[next].times[p.clocks] ||
						p.nodes[next].stall >= 0 || p.nodes[next].idle)
						break;
					at = next;
				}
				p.optimize(program, b);
				p.blocks.push_back(std::move(b));
			}
			return p;
		}

	private:
		// Bits 0..3 / 4..7 are A/B history[0..2] and current; bit 8 is
		// lastMulInputA. Every frame exit observes all architectural values.
		static constexpr unsigned allValues = 0x1ff, lastInput = 0x100;
		static unsigned pipeBit(bool b) { return 1u << (b ? 4 : 0); }
		unsigned transferValues(const ProgramSnapshot& program, Node& n, unsigned out, bool stall)
		{
			if (n.idle)
				return out;
			const auto& in = program.instruction(core, n.pc);
			unsigned live = 0;
			bool arithmetic = false;
			for (unsigned b = 0; b < 2; ++b)
			{
				const unsigned shift = b * 4, bits = (out >> shift) & 15;
				unsigned input = ((bits & 1) << 1) | ((bits & 2) << 1);
				const bool changed = !stall && in.op != Op::Nop && in.accB == bool(b);
				const bool currentNeeded = (bits & 12) || (!b && n.sample >= 0);
				if (currentNeeded)
				{
					if (!changed || !in.replace)
						input |= 8;
					arithmetic |= changed;
				}
				live |= input << shift;
			}
			// Coefficient publication precedes even a GRAM stall.
			if (in.coefWriteMask & (1 << core))
				live |= pipeBit(in.coefFromB);
			if (stall || in.op == Op::Nop)
			{
				live |= out & lastInput;
				if (!stall)
				{
					n.deadMac = true;
					n.keepInput = false;
				}
				return live;
			}
			n.deadMac = !arithmetic;
			n.keepInput = (out & lastInput) != 0;
			switch (in.op)
			{
			case Op::StoreSat:
			case Op::GramStoreReplace:
			case Op::GramStoreAdd:
				live |= pipeBit(in.storeB);
				break;
			case Op::StoreRaw:
			case Op::StoreRect:
			case Op::StoreFoldPos:
			case Op::StoreFoldNeg:
				live |= pipeBit(false);
				break;
			case Op::Mul30:
				if (uint8_t(in.coef) & 4)
					live |= pipeBit(in.accB);
				break;
			case Op::Special:
				if (in.control)
				{
					const unsigned sub = in.sub;
					if ((sub < 3 && program.conditionalBranches) || sub == 4 || sub == 5 || sub == 10 ||
						(core == 1 && (sub == 7 || sub == 11)))
						live |= pipeBit(in.accB);
					if (sub == 6 && (arithmetic || n.keepInput))
						live |= lastInput;
				}
				break;
			default:
				break;
			}
			return live;
		}
		void analyzeValues(const ProgramSnapshot& program)
		{
			bool changed;
			do
			{
				changed = false;
				for (size_t i = nodes.size(); i-- > 0;)
				{
					auto& n = nodes[i];
					unsigned out = 0;
					for (int dst : {n.next, n.taken})
						if (dst >= 0)
							out |= nodes[dst].liveIn;
					n.liveOut = out;
					unsigned live = transferValues(program, n, out, false);
					if (n.stall >= 0)
						live |= transferValues(program, n, nodes[n.stall].liveIn, true);
					if (n.times[clocks])
						live |= allValues;
					changed |= live != n.liveIn;
					n.liveIn = live;
				}
			}
			while (changed);
		}
		void analyzeEramValues(const ProgramSnapshot& program)
		{
			// Captured write sample/packed code have no external observer mid-frame.
			// Keep a capture if any path reads it before replacement or exits.
			std::vector<unsigned> live(nodes.size());
			auto transfer = [&](EramStep& e, unsigned out)
			{
				if (e.address)
				{
					const bool write = e.before.nextMode == 0x10;
					const bool normal = !program.staticBus && program.memoryMode != 0x60;
					const bool rawUse = normal && write && (!program.rawSingle() || e.transfer) && !program.compress;
					const bool packedUse = normal && write && program.compress;
					e.saveWriteSample = (out & 1) || rawUse;
					e.saveWritePacked = (out & 2) || packedUse;
					out = 0; // Both captures are definitions, independent of their old values.
				}
				if (e.before.currentMode == 0x10)
				{
					if (e.low && e.transfer && !e.before.shortCurrent)
						out |= 1;
					// The high-byte merge reads the old packed low byte.
					if (program.compress && (e.low || (e.commit && e.transfer && !program.rawSingle())))
						out |= 2;
				}
				return out;
			};
			bool changed;
			do
			{
				changed = false;
				for (size_t i = nodes.size(); i-- > 0;)
				{
					auto& n = nodes[i];
					unsigned out = 0;
					for (int dst : {n.next, n.taken})
						if (dst >= 0)
							out |= live[dst];
					unsigned input = transfer(n.eram, out);
					if (n.times[clocks])
						input |= 3; // Preserve partial-frame/tail handoff.
					changed |= input != live[i];
					live[i] = input;
				}
			}
			while (changed);
		}
		void analyzeReadLatch(const ProgramSnapshot& program)
		{
			// Normal compressed phases communicate through readPacked. Only DSP
			// reads, frame exits and the first conditional hold observe readLatch.
			if (!program.compress || program.extended())
				return;
			std::vector<bool> live(nodes.size());
			bool changed;
			do
			{
				changed = false;
				for (size_t i = nodes.size(); i-- > 0;)
				{
					auto& n = nodes[i];
					auto& e = n.eram;
					bool needed = false;
					for (int dst : {n.next, n.taken})
						if (dst >= 0)
							needed = needed || live[dst];
					if (!n.idle)
					{
						const auto& in = program.instruction(core, n.pc);
						needed |= in.control && in.sub >= 12;
					}
					e.readLatchStages = 0;
					const bool write = e.before.nextMode == 0x10;
					if (e.address && (write || e.before.shortNext || e.earlyHold != 1))
					{
						if (needed)
							e.readLatchStages |= EramStep::Address;
						// Unknown hold selects the old latch without changing it.
						if (write || e.before.shortNext || e.earlyHold >= 0)
							needed = false;
					}
					if (e.commit && e.transfer)
					{
						if (needed)
							e.readLatchStages |= EramStep::Commit;
						needed = false;
					}
					if (e.low)
					{
						if (needed)
							e.readLatchStages |= EramStep::Low;
						needed = false;
					}
					needed = needed || n.times[clocks];
					changed |= needed != live[i];
					live[i] = needed;
				}
			}
			while (changed);
		}
		void optimize(const ProgramSnapshot& program, Block& b)
		{
			const unsigned count = unsigned(b.ops.size());
			auto instr = [&](unsigned i) -> const Instr& { return program.instruction(core, nodes[b.ops[i].node].pc); };
			if (nodes[b.ops[0].node].idle)
				return;
			b.liveOut = nodes[b.ops.back().node].liveOut;
			// Intermediate controller flags are unobservable within a basic block.
			// Keep the final write of each flag for architectural state at its edge.
			unsigned writtenFlags = 0;
			for (unsigned i = count; i-- > 0;)
			{
				auto& eram = nodes[b.ops[i].node].eram;
				const unsigned writes = eram.publishFlags;
				eram.publishFlags &= ~writtenFlags;
				writtenFlags |= writes;
			}
			for (unsigned i = 0; i < count; ++i)
			{
				const auto& in = instr(i);
				auto& op = b.ops[i];
				const auto& node = nodes[op.node];
				op.dead = node.deadMac;
				op.keepInput = node.keepInput;
				// Built-in constants feed ordinary arithmetic unless the opcode
				// replaces the operand (notably GRAM reads and stores).
				const bool constant = in.useConstant &&
					(in.op == Op::Mac || in.op == Op::Op54 || in.op == Op::Cmp || in.op == Op::GramStoreReplace ||
					 in.op == Op::Special);
				if (constant && !(node.predicate.pos & 1) && !(node.predicate.neg & 1))
					op.constantShift = 4 + 6 * (in.mem - 1);
				op.operand24 = constant || in.op == Op::StoreSat || in.op == Op::GramStoreAdd ||
					in.op == Op::StoreRaw || in.op == Op::StoreRect || in.op == Op::StoreFoldPos ||
					in.op == Op::StoreFoldNeg || in.op == Op::Invert ||
					(in.op == Op::Mul30 && ((uint8_t(in.coef) & 4) || in.useConstant));
			}
			// Rotate each accumulator's pipeline through four registers of the JitEmitter map.
			unsigned history[2][3] = {{12, 13, 14}, {15, 16, 17}}, current[2] = {26, 27};
			for (unsigned i = 0; i < count; ++i)
			{
				auto& op = b.ops[i];
				const auto& in = instr(i);
				for (unsigned acc = 0; acc < 2; ++acc)
				{
					op.pipe[acc] = history[acc][0];
					op.current[acc] = current[acc];
				}
				const unsigned acc = in.accB;
				op.destination = current[acc];
				if (in.op != Op::Nop && !op.dead)
				{
					for (unsigned r : {acc ? 15u : 12u, acc ? 16u : 13u, acc ? 17u : 14u, acc ? 27u : 26u})
						if (r != current[acc] && r != history[acc][1] && r != history[acc][2])
						{
							op.destination = r;
							break;
						}
					current[acc] = op.destination;
				}
				for (unsigned a = 0; a < 2; ++a)
				{
					history[a][0] = history[a][1];
					history[a][1] = history[a][2];
					history[a][2] = current[a];
				}
			}
			for (unsigned a = 0; a < 2; ++a)
			{
				b.current[a] = current[a];
				for (unsigned k = 0; k < 3; ++k)
					b.history[a][k] = history[a][k];
			}
		}
	};
} // namespace esp
