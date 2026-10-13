#pragma once

#include "esp_core_program.h"
#include "esp_jit.h"

#include <algorithm>
#include <atomic>
#include <bitset>
#include <cstdio>
#include <cstdlib>
#include <vector>

#if CHIPS_JIT_ASMJIT
#include <asmjit/asmjit.h>

namespace esp
{
	enum class Cond { Eq, Ne, Lt, Ge, Lo, Ls };

	// An instruction the assembler rejects must fail the compile, never vanish.
	struct EmitErrors final : asmjit::ErrorHandler
	{
		bool failed = false;
		void handleError(asmjit::Error, const char*, asmjit::BaseEmitter*) override { failed = true; }
	};

	// Lowers the two CoreProgram graphs of a frame to one native function. Everything
	// here is shared by the back ends; an Isa provides the register file, the primitive
	// operations and the few lowerings that differ per instruction set (product
	// formation, block-edge normalization, the retained multiplicand's register, the
	// saturation clamp and its cold code, prologue, epilogue and code validation).
	//
	// Register numbers are the arm64 map, virtual registers on x86: 19 frame, 20 runtime,
	// 21 elapsed clocks / cached packed write latch, 22 IRAM (GRAM at a fixed distance),
	// 23 coefficients (shifts at a fixed distance), 24 core, 25 ERAM state, 26/27 current
	// accumulators, 28 program words, 29 last multiplicand, 30 packed ERAM codes,
	// 12..17 accumulator history, 0..11 temporaries.
	template <class Isa>
	class JitEmitter : protected Isa
	{
		static constexpr int N = Isa::lg2eramSize;
		using R = Runtime<N>;
		using L = RuntimeLayout<N>;
		using E = EramRuntime<N>;
		using Gp = typename Isa::Gp;
		using Mem = typename Isa::Mem;
		using F = CoreProgram::EramStep;

		using Isa::a;
		using Isa::w;
		using Isa::x;
		using Isa::mem;
		using Isa::core;
		using Isa::indexed;
		using Isa::predicateMemory;
		using Isa::load;
		using Isa::loadByte;
		using Isa::loadHalf;
		using Isa::store;
		using Isa::storeByte;
		using Isa::storeHalf;
		using Isa::storeZero;
		using Isa::add;
		using Isa::sub;
		using Isa::bitAnd;
		using Isa::bitOr;
		using Isa::bitXor;
		using Isa::bitClear;
		using Isa::shl;
		using Isa::shr;
		using Isa::sar;
		using Isa::orShifted;
		using Isa::addShifted;
		using Isa::signedBits;
		using Isa::bits;
		using Isa::invert;
		using Isa::negate;
		using Isa::select;
		using Isa::selectZero;
		using Isa::setCondition;
		using Isa::jump;
		using Isa::jumpZero;
		using Isa::jumpNonzero;
		using Isa::jumpIfBit;
		using Isa::jumpIfEqual;
		using Isa::saturate;
		using Isa::packEramNative;
		using Isa::multiplyByte;

		const ProgramSnapshot& program;
		unsigned clocks;
		CoreProgram graphs[2];
		const CoreProgram::Operation* operation = nullptr;
		bool arbitration = false;
		// w21 holds the packed write latch when no clock counter is needed. Validity is
		// a compile-time fact of a straight-line block; a DSP write or an edge ends it.
		bool cacheWritePacked = false, writePackedValid = false;
		unsigned predicateStack = 0;
		// Every sample a kernel stores is a 24-bit value, so a slot it stored on every path to a
		// read needs no sign extension there; the retained multiplicand carries the same fact.
		// Must-facts over the block graph: a block's entry facts are the meet of its
		// predecessors' exit facts, the core's entry block starts without any.
		struct Facts
		{
			std::bitset<IRAM_SIZE> iram, gram;
			bool lastInput = false;

			bool operator==(const Facts& o) const { return iram == o.iram && gram == o.gram && lastInput == o.lastInput; }
			void meet(const Facts& o)
			{
				iram &= o.iram;
				gram &= o.gram;
				lastInput &= o.lastInput;
			}
			static bool replacesOperand(const Instr& in)
			{
				return in.op == Op::GramRead || in.op == Op::StoreSat || in.op == Op::GramStoreAdd ||
					in.op == Op::StoreRaw || in.op == Op::StoreRect || in.op == Op::StoreFoldPos ||
					in.op == Op::StoreFoldNeg || (in.op == Op::Mul30 && (uint8_t(in.coef) & 4)) ||
					(in.control && (in.sub == 6 || in.sub >= 12));
			}
			// Whether the operation's product operand is a 24-bit value: the optimizer's own
			// fact, or one established on the way here.
			bool operand24(const Instr& in, const CoreProgram::Operation& op) const
			{
				if (op.operand24)
					return true;
				if (!((!op.dead && op.constantShift < 0) || op.keepInput))
					return false;
				if (in.control && in.sub == 6)
					return lastInput;
				if (in.control && in.sub >= 12)
					return true; // the read latch is a 24-bit sample wherever it is set, or zero
				if (in.op == Op::GramRead)
					return gram[in.mem];
				if (replacesOperand(in))
					return false;
				if (in.useConstant)
					return true;
				return iram[in.mem];
			}
			// The operation's stores and its retained multiplicand, after emitArithmetic's writes.
			void apply(const Instr& in, const CoreProgram::Operation& op, bool operand24)
			{
				if (in.op == Op::Nop)
					return;
				switch (in.op)
				{
				case Op::StoreSat:
				case Op::StoreRaw:
				case Op::StoreRect:
				case Op::StoreFoldPos:
				case Op::StoreFoldNeg:
					iram[in.mem] = true;
					break;
				case Op::GramStoreReplace:
				case Op::GramStoreAdd:
					gram[in.mem] = true;
					break;
				case Op::Mul30:
					if (uint8_t(in.coef) & 4)
						iram[in.mem] = true;
					break;
				case Op::Special:
					if (in.control && in.sub >= 12)
						iram[in.mem | 0xf0] = true;
					break;
				default:
					break;
				}
				if (op.keepInput)
					lastInput = operand24;
			}
		};
		Facts facts;					// of the operation being emitted
		std::vector<Facts> blockFacts;	// at each block's entry, for the core being emitted
		// Pipeline registers a product wrote in the block need the 30-bit wrap at its edge,
		// the others arrived wrapped.
		unsigned blockWritten = 0;

		void computeBlockFacts(unsigned c)
		{
			const auto& graph = graphs[c];
			Facts all;
			all.iram.set();
			all.gram.set();
			all.lastInput = true;
			blockFacts.assign(graph.blocks.size(), all);
			blockFacts[graph.owner[0]] = Facts{};
			bool changed = true;
			auto propagate = [&](int node, const Facts& f)
			{
				if (node < 0)
					return;
				auto& target = blockFacts[graph.owner[unsigned(node)]];
				const Facts before = target;
				target.meet(f);
				changed |= !(before == target);
			};
			while (changed)
			{
				changed = false;
				for (unsigned bi = 0; bi < graph.blocks.size(); ++bi)
				{
					const auto& block = graph.blocks[bi];
					const auto& first = graph.nodes[block.ops.front().node];
					// The stall edge leaves before the first operation, with the entry facts.
					propagate(first.stall, blockFacts[bi]);
					if (first.idle)
					{
						propagate(first.next, blockFacts[bi]);
						continue;
					}
					Facts out = blockFacts[bi];
					for (const auto& op : block.ops)
					{
						const auto& in = program.instruction(c, graph.nodes[op.node].pc);
						out.apply(in, op, out.operand24(in, op));
					}
					const auto& last = graph.nodes[block.ops.back().node];
					if (last.branch)
					{
						propagate(last.taken, out);
						if (last.conditional)
							propagate(last.next, out);
					}
					else
						propagate(last.next, out);
				}
			}
		}
		void beginBlockFacts(unsigned bi)
		{
			facts = blockFacts[bi];
			blockWritten = 0;
		}
		// GRAM rides on the IRAM base: both rings share one position (batchedFrameReady), so a
		// core's GRAM slots sit at a fixed distance from its IRAM slots.
		Mem gramMem(unsigned c, unsigned slot)
		{
			return mem(22, offsetof(R, gram) - (offsetof(R, core) + c * sizeof(CoreRuntime) + offsetof(CoreRuntime, iram)) + slot * 4);
		}

		JitEmitter(typename Isa::Assembler& _a, const ProgramSnapshot& _program, unsigned _clocks)
			: Isa(_a), program(_program), clocks(_clocks)
		{
		}

		static size_t acc(bool b) { return b ? offsetof(CoreRuntime, accB) : offsetof(CoreRuntime, accA); }

		void loadPipe(bool b, unsigned dst, bool saturated = false)
		{
			signedBits(w(dst), w(operation->pipe[b]), 0, 30);
			if (saturated)
				saturate(dst);
		}
		void liveWord(unsigned pc, unsigned dst) { load(w(dst), mem(28, pc * 4)); }

		void transformCoefficient(unsigned transform)
		{
			if (transform == 8)
				invert(w(1), w(1));
			else if (transform == 16)
			{
				bitAnd(w(9), w(1), 0x7fffff);
				invert(w(9), w(9));
				bitAnd(w(10), w(9), 0x7fffff);
				a.cmp(w(1), 0);
				select(w(1), w(10), w(9), Cond::Ge);
			}
		}
		// Register index in w1, coefficient out in w1; w0 is preserved. The frame's register view
		// resolves index 7 to register 5 and index 6 to the variable-offset field.
		void coefficient(int transform)
		{
			addShifted(x(10), x(19), x(1), 2);
			load(w(1), mem(10, offsetof(JitFrame, dmacCoef)));
			if (transform >= 0)
			{
				transformCoefficient(unsigned(transform));
				return;
			}
			loadByte(w(11), core(offsetof(CoreRuntime, lastCoefficientTransform)));
			invert(w(9), w(1));
			a.cmp(w(11), 8);
			select(w(1), w(9), w(1), Cond::Eq);
			bitAnd(w(9), w(1), 0x7fffff);
			invert(w(9), w(9));
			bitAnd(w(10), w(9), 0x7fffff);
			a.cmp(w(1), 0);
			select(w(9), w(10), w(9), Cond::Ge);
			a.cmp(w(11), 16);
			select(w(1), w(9), w(1), Cond::Eq);
		}
		void fixedCoefficient(unsigned flags)
		{
			load(w(1), mem(19, offsetof(JitFrame, dmacCoef) + (flags >> 5) * 4));
			transformCoefficient(flags & 0x18);
		}
		// A write to a coefficient register or the variable offset also lands in the frame's
		// register view.
		void publishDmacCoefficient(unsigned index, unsigned reg)
		{
			if (index <= 5)
				store(w(reg), mem(19, offsetof(JitFrame, dmacCoef) + index * 4));
			if (index == 5)
				store(w(reg), mem(19, offsetof(JitFrame, dmacCoef) + 7 * 4));
		}
		void publishVarOffsetField(unsigned reg, unsigned scratch)
		{
			shl(w(scratch), w(reg), 11);
			bitAnd(w(scratch), w(scratch), 0x7fffff);
			store(w(scratch), mem(19, offsetof(JitFrame, dmacCoef) + 6 * 4));
		}

		// w6 sample -> w7 packed code; no caller reads w6 afterwards.
		void packWriteLatchNative()
		{
			if (cacheWritePacked && writePackedValid)
			{
				a.mov(w(7), w(21));
				return;
			}
			packEramNative();
			if (cacheWritePacked)
			{
				a.mov(w(21), w(7));
				writePackedValid = true;
			}
		}
		// w7 packed code -> w6 signed sample: the mantissa times 2^(4 + 2 * exponent)
		void unpackEramNative()
		{
			Isa::exponentShift(w(8), w(7));
			signedBits(w(6), w(7), 0, 14);
			shl(w(6), w(6), w(8));
		}
		// w7 -> readPacked, and readLatch when published
		void setPackedNative(bool publishLatch = true)
		{
			storeHalf(w(7), mem(25, offsetof(E, readPacked)));
			if (!publishLatch)
				return;
			unpackEramNative();
			store(w(6), mem(25, offsetof(E, readLatch)));
		}
		// w5 address -> w7 packed code
		void readPackedNative()
		{
			bitAnd(w(5), w(5), E::mask);
			loadHalf(w(7), indexed(x(30), x(5), 1));
		}
		// w5 address, w7 packed code: one byte of it lands in the stored code, the other byte
		// is the stored code's. Packed codes are 16-bit values, so the byte insert leaves the
		// upper half zero as the masks did.
		void writePackedNative(bool high)
		{
			bitAnd(w(5), w(5), E::mask);
			loadHalf(w(8), indexed(x(30), x(5), 1));
			Isa::insertByte(w(7), w(8), !high, w(9));
			storeHalf(w(7), indexed(x(30), x(5), 1));
			unpackEramNative();
			store(w(6), indexed(Isa::eramMemory(), x(5), 2));
		}
		// w5 address -> w6 value
		void rawRead()
		{
			bitAnd(w(5), w(5), E::mask);
			load(w(6), indexed(Isa::eramMemory(), x(5), 2));
		}
		// w5 address, w6 sample
		void writePhaseNative(bool high, bool currentWriteLatch = false)
		{
			if (program.compress)
			{
				if (currentWriteLatch)
					packWriteLatchNative();
				else
					packEramNative();
				writePackedNative(high);
				return;
			}
			bitAnd(w(5), w(5), E::mask);
			const Gp memory = Isa::eramMemory();
			if (!program.rawSingle())
			{
				load(w(7), indexed(memory, x(5), 2));
				bitAnd(w(7), w(7), high ? 0xfffu : 0xfff000u);
				bitAnd(w(6), w(6), high ? 0xfff000u : 0xfffu);
				bitOr(w(6), w(6), w(7));
			}
			signedBits(w(6), w(6), 0, 24);
			store(w(6), indexed(memory, x(5), 2));
		}
		// writePackedNext takes the high byte of the packed code in w7
		void mergeWritePackedNext()
		{
			loadHalf(w(8), mem(25, offsetof(E, writePackedNext)));
			Isa::insertByte(w(8), w(7), true, w(9));
			storeHalf(w(8), mem(25, offsetof(E, writePackedNext)));
		}

		void samplePredicate(int sample, unsigned currentA)
		{
			if (sample < 0)
				return;
			bits(w(0), w(currentA), 29, 1);
			storeByte(w(0), predicateMemory(sample));
		}
		void emitPredicateSample(unsigned c, unsigned currentA)
		{
			samplePredicate(graphs[c].nodes[operation->node].sample, currentA);
		}

		void emitArithmetic(unsigned c, unsigned pc)
		{
			const auto& in = program.instruction(c, pc);
			if (in.op == Op::Nop)
			{
				emitPredicateSample(c, operation->current[0]);
				return;
			}
			const bool dead = operation->dead;
			const bool needsOperand = (!dead && operation->constantShift < 0) || operation->keepInput;
			const bool replacesOperand = Facts::replacesOperand(in);
			const auto& predicate = graphs[c].nodes[operation->node].predicate;
			const bool gated = (predicate.pos & 1) || (predicate.neg & 1);
			const bool dmac = in.control && in.sub == 6;
			// The retained multiplicand takes the operand at its source when nothing rewrites it on
			// the way, instead of a copy after the fact.
			const bool direct = operation->keepInput && needsOperand && !gated && in.op != Op::Invert &&
				(!replacesOperand || in.op == Op::GramRead || dmac);
			const unsigned operandReg = direct ? 29 : 0;
			const bool operand24 = facts.operand24(in, *operation);
			if (direct && !dmac) // DMAC shifts the retained input in place (shiftLastInput)
				Isa::beginLastInput();
			if (needsOperand && !replacesOperand)
			{
				if (in.useConstant)
					a.mov(w(operandReg), in.constant);
				else
					load(w(operandReg), mem(22, in.mem * 4));
			}
			bool specialCoef = false;
			switch (in.op)
			{
			case Op::StoreSat:
				loadPipe(in.storeB, 0, true);
				store(w(0), mem(22, in.mem * 4));
				break;
			case Op::GramRead:
				if (needsOperand)
					load(w(operandReg), gramMem(c, in.mem));
				break;
			case Op::GramStoreReplace:
				loadPipe(in.storeB, 2, true);
				store(w(2), gramMem(c, in.mem));
				break;
			case Op::GramStoreAdd:
				loadPipe(in.storeB, 0, true);
				store(w(0), gramMem(c, in.mem));
				break;
			case Op::Mul30:
			{
				const auto flags = uint8_t(in.coef);
				if (flags & 4)
				{
					if ((flags & 0x1c) == 0x1c)
					{
						// Saturation to the wrapped accumulator's sign.
						signedBits(w(0), w(operation->pipe[in.accB]), 29, 1);
						bitXor(w(0), w(0), 0x7fffff);
					}
					else
						loadPipe(in.accB, 0, true);
					store(w(0), mem(22, in.mem * 4));
				}
				if (!dead)
				{
					fixedCoefficient(flags);
					Isa::signedByte(w(1), w(1), 16);
				}
				specialCoef = true;
				break;
			}
			case Op::Special:
				if (!in.control)
					break;
				switch (in.sub)
				{
				case 0:
				case 1:
				case 2:
				case 3:
					// The CFG owns the redirect and delay slots; only the predicate
					// crosses to the block edge, in w4.
					if (program.conditionalBranches && in.sub != 3)
					{
						loadPipe(in.accB, 2);
						a.cmp(w(2), 0);
						setCondition(w(4), in.sub == 0 ? Cond::Eq : in.sub == 1 ? Cond::Lt : Cond::Ge);
					}
					break;
				case 4:
				case 5:
					bits(w(2), w(operation->pipe[in.accB]), 29, 1);
					storeByte(w(2), core(in.sub == 4 ? offsetof(CoreRuntime, dintPin) : offsetof(CoreRuntime, irq1Sign)));
					break;
				case 6:
				{
					if (needsOperand)
					{
						if (direct)
							Isa::shiftLastInput(7);
						else
							sar(w(0), w(29), 7);
					}
					if (dead)
						break;
					liveWord(pc, 1);
					const auto& state = graphs[c].nodes[operation->node].state;
					auto plain = a.newLabel();
					if (state.lastMul30 < 0)
					{
						loadByte(w(11), core(offsetof(CoreRuntime, lastMul30)));
						jumpZero(w(11), plain);
					}
					if (state.lastMul30 != 0)
					{
						bits(w(1), w(1), 5, 3);
						coefficient(state.lastTransform);
						bits(w(1), w(1), 9, 7); // a 7-bit coefficient, product-ready as it is
					}
					if (state.lastMul30 <= 0)
					{
						// Without a preceding MUL30 the word's low byte is the coefficient. The join
						// passes a 7-bit register coefficient through unchanged.
						a.bind(plain);
						Isa::signedByte(w(1), w(1), 0);
					}
					specialCoef = true;
					break;
				}
				case 7:
					if (c == 1)
					{
						loadPipe(in.accB, 2);
						store(w(2), mem(20, L::varOffset));
						publishVarOffsetField(2, 3);
					}
					break;
				case 10:
					loadPipe(in.accB, 2, true);
					store(w(2), mem(20, L::mailbox));
					break;
				case 11:
					if (c == 1)
					{
						loadPipe(in.accB, 2, true);
						store(w(2), mem(20, L::writeLatch));
						writePackedValid = false;
					}
					break;
				case 12:
				case 13:
				case 14:
				case 15:
					if (c == 1)
						load(w(0), mem(20, L::readLatch));
					else
						a.mov(w(0), 0);
					store(w(0), mem(22, (in.mem | 0xf0) * 4));
					break;
				default:
					break;
				}
				break;
			case Op::StoreRaw:
				signedBits(w(0), w(operation->pipe[0]), 0, 24);
				store(w(0), mem(22, in.mem * 4));
				break;
			case Op::StoreRect:
				loadPipe(false, 0, true);
				a.cmp(w(0), 0);
				selectZero(w(0), w(0), Cond::Ge);
				store(w(0), mem(22, in.mem * 4));
				break;
			case Op::StoreFoldPos:
			case Op::StoreFoldNeg:
				loadPipe(false, 0, true);
				store(w(0), mem(22, in.mem * 4));
				if (needsOperand)
				{
					invert(w(2), w(0));
					a.cmp(w(0), 0);
					select(w(0), w(2), w(0), in.op == Op::StoreFoldPos ? Cond::Ge : Cond::Lt);
					bitAnd(w(0), w(0), 0x7fffff);
				}
				break;
			case Op::Invert:
				if (needsOperand)
				{
					invert(w(0), w(0));
					bitAnd(w(0), w(0), 0x7fffff);
				}
				break;
			default:
				break;
			}
			if (needsOperand && gated)
			{
				const bool pos = predicate.pos & 1, neg = predicate.neg & 1;
				if (pos && neg)
					a.mov(w(0), 0);
				else
				{
					if (predicate.govern >= 0)
						loadByte(w(2), predicateMemory(predicate.govern));
					else
						loadByte(w(2), core(offsetof(CoreRuntime, compareNegative)));
					a.cmp(w(2), 0);
					selectZero(w(0), w(0), pos ? Cond::Ne : Cond::Eq);
				}
			}
			if (operation->keepInput && !direct)
				Isa::setLastInput();
			facts.apply(in, *operation, operand24);
			if (!dead)
			{
				Isa::emitProduct(*operation, in, pc, specialCoef, operandReg, operand24);
				blockWritten |= 1u << operation->destination;
			}
			emitPredicateSample(c, !dead && !in.accB ? operation->destination : operation->current[0]);
		}

		void extendedTransfer(unsigned mask, bool write)
		{
			if (!mask || (write && program.staticBus && (program.rawSingle() || program.memoryMode == 0x60)))
				return;
			if (write)
			{
				load(w(6), mem(25, offsetof(E, writeLatch)));
				if (program.compress)
					packWriteLatchNative();
				else
					a.mov(w(7), w(6));
			}
			else if (program.staticBus)
				return; // Transfers the existing latch to itself.
			else
			{
				load(w(5), mem(25, offsetof(E, effectiveAddr)));
				if (program.compress)
					readPackedNative();
				else
				{
					rawRead();
					packEramNative();
				}
			}
			if (program.compress)
				loadHalf(w(8), mem(25, offsetof(E, readPacked)));
			else
				load(w(8), mem(25, offsetof(E, readLatch)));
			bitAnd(w(8), w(8), ~mask);
			bitAnd(w(7), w(7), mask);
			bitOr(w(7), w(7), w(8));
			if (program.compress)
				setPackedNative();
			else
			{
				signedBits(w(7), w(7), 0, 24);
				store(w(7), mem(25, offsetof(E, readLatch)));
			}
		}
		void eramImmediate(const CoreProgram::Controller& state)
		{
			if (!state.immediateKnown)
				return;
			if (state.immediateTable >= 0)
			{
				load(x(10), mem(19, offsetof(JitFrame, eramImm)));
				load(w(3), mem(10, unsigned(state.immediateTable) * 4));
				if (state.immediateMask != 0xfffff)
					bitAnd(w(3), w(3), state.immediateMask);
			}
			else
			{
				a.mov(w(3), 0);
				for (unsigned i = 0; i < 4; ++i)
				{
					if (state.immediatePc[i] < 0)
						continue;
					liveWord(unsigned(state.immediatePc[i]), 4);
					bits(w(4), w(4), 23, 5);
					addShifted(w(3), w(3), w(4), i * 5);
				}
			}
			store(w(3), mem(25, offsetof(E, immOffsetAccNext)));
		}
		void eramVariable(unsigned mask)
		{
			if (!mask)
				return;
			load(w(3), mem(25, offsetof(E, varOffset)));
			shr(w(3), w(3), 12);
			bitAnd(w(3), w(3), mask);
			load(w(4), mem(25, offsetof(E, varOffsetNext)));
			bitOr(w(3), w(3), w(4));
			store(w(3), mem(25, offsetof(E, varOffsetNext)));
		}
		void publishEramFlags(const F& step)
		{
			auto flag = [&](unsigned mask, size_t offset, bool value)
			{
				if (!(step.publishFlags & mask))
					return;
				if (!value)
				{
					storeZero(mem(25, offset), 1);
					return;
				}
				a.mov(w(3), 1);
				storeByte(w(3), mem(25, offset));
			};
			flag(F::ActiveNext, offsetof(E, activeNext), step.restart || !step.address);
			flag(F::ActiveCurrent, offsetof(E, activeCurrent),
				step.address && !(program.rawSingle() && step.before.nextMode != 0x10 && !program.extended()));
			flag(F::ShortNext, offsetof(E, shortNext), step.restart);
			flag(F::ShortCurrent, offsetof(E, shortCurrent), step.before.shortNext);
		}

		// Every stage and command mode here is optimizer output; the only live operands
		// are host-editable address bits and DSP-produced samples.
		void emitEram(const F& step, unsigned pc, unsigned elapsed = 0, bool advanceClock = true)
		{
			publishEramFlags(step);
			if (step.address)
				eramImmediate(step.before);
			if (step.start || step.defer || step.restart)
			{
				if (pc < PRAM_SIZE)
				{
					liveWord(pc, 0);
					bits(w(0), w(0), 23, 5);
				}
				else
					a.mov(w(0), 0);
			}
			// Blocks publish the clock once on their outgoing edge; only transaction
			// starts need a timestamp inside the block.
			if (advanceClock || step.start || step.restart)
			{
				load(x(2), mem(25, offsetof(E, cycle)));
				if (elapsed)
					add(x(2), x(2), elapsed);
				if (advanceClock)
				{
					add(x(3), x(2), 1);
					store(x(3), mem(25, offsetof(E, cycle)));
				}
			}
			if (step.start)
			{
				storeByte(w(0), mem(25, offsetof(E, modeNext)));
				store(x(2), mem(25, offsetof(E, cycleStartNext)));
				add(x(3), x(2), program.spacing);
				store(x(3), mem(25, offsetof(E, nextCommandCycle)));
				storeZero(mem(25, offsetof(E, immOffsetAccNext)), 4);
				storeZero(mem(25, offsetof(E, varOffsetNext)), 4);
			}
			eramVariable(step.capture);
			if (step.defer)
				storeByte(w(0), mem(25, offsetof(E, deferredCommand)));
			extendedTransfer(step.extendedMask, step.before.currentMode == 0x10);
			if (step.low)
				emitEramLow(step);
			if (step.commit)
				emitEramCommit(step);
			if (step.address)
				emitEramAddress(step);
			eramVariable(step.captureAfter);
		}
		void emitEramLow(const F& step)
		{
			if (step.before.currentMode == 0x10)
			{
				if (step.transfer && !step.before.shortCurrent)
				{
					load(w(5), mem(25, offsetof(E, lowEffectiveAddr)));
					load(w(6), mem(25, offsetof(E, writeLatchNext)));
					writePhaseNative(false);
				}
				load(w(6), mem(25, offsetof(E, writeLatch)));
				if (program.compress)
				{
					packWriteLatchNative();
					mergeWritePackedNext();
					setPackedNative(step.readLatchStages & F::Low);
				}
				else
				{
					signedBits(w(6), w(6), 0, 24);
					store(w(6), mem(25, offsetof(E, readLatch)));
				}
			}
			else if (program.compress)
			{
				// The code's low byte doubled up; its address is the halfword's on both JIT hosts.
				loadByte(w(7), mem(25, offsetof(E, readPackedNext)));
				orShifted(w(7), w(7), w(7), 8);
				setPackedNative(step.readLatchStages & F::Low);
			}
			else
			{
				load(w(6), mem(25, offsetof(E, readRawNext)));
				bitAnd(w(6), w(6), 0xfff);
				orShifted(w(6), w(6), w(6), 12);
				signedBits(w(6), w(6), 0, 24);
				store(w(6), mem(25, offsetof(E, readLatch)));
			}
		}
		void emitEramCommit(const F& step)
		{
			if (program.rawSingle())
			{
				if (step.before.currentMode == 0x10)
				{
					load(w(6), mem(25, offsetof(E, writeLatch)));
					signedBits(w(6), w(6), 0, 24);
					store(w(6), mem(25, offsetof(E, readLatch)));
				}
				return;
			}
			if (!step.transfer)
				return;
			load(w(5), mem(25, offsetof(E, effectiveAddr)));
			if (step.before.currentMode == 0x10)
			{
				load(w(6), mem(25, offsetof(E, writeLatch)));
				writePhaseNative(true, true);
				if (program.compress)
				{
					load(w(6), mem(25, offsetof(E, writeLatch)));
					packWriteLatchNative();
					mergeWritePackedNext();
					loadHalf(w(8), mem(25, offsetof(E, readPacked)));
					Isa::insertByte(w(7), w(8), false, w(9));
					setPackedNative(step.readLatchStages & F::Commit);
				}
				else
				{
					load(w(6), mem(25, offsetof(E, writeLatch)));
					load(w(7), mem(25, offsetof(E, readLatch)));
					bitAnd(w(7), w(7), 0xfff);
					bitAnd(w(6), w(6), 0xfff000);
					bitOr(w(6), w(6), w(7));
					signedBits(w(6), w(6), 0, 24);
					store(w(6), mem(25, offsetof(E, readLatch)));
				}
			}
			else if (program.compress)
			{
				readPackedNative();
				bitAnd(w(7), w(7), 0xff00);
				loadHalf(w(8), mem(25, offsetof(E, readPackedNext)));
				bitAnd(w(8), w(8), 0xff);
				bitOr(w(7), w(7), w(8));
				setPackedNative(step.readLatchStages & F::Commit);
			}
			else
			{
				rawRead();
				bitAnd(w(6), w(6), 0xfff000);
				load(w(7), mem(25, offsetof(E, readRawNext)));
				bitAnd(w(7), w(7), 0xfff);
				bitOr(w(6), w(6), w(7));
				signedBits(w(6), w(6), 0, 24);
				store(w(6), mem(25, offsetof(E, readLatch)));
			}
		}
		void emitEramAddress(const F& step)
		{
			const bool write = step.before.nextMode == 0x10;
			load(x(3), mem(25, offsetof(E, cycleStartNext)));
			// Only a frame's first transaction depends on the preceding frame's timing.
			if (step.earlyHold < 0 && !step.before.shortNext && program.compress && !write)
			{
				load(x(4), mem(25, offsetof(E, cycleStartCurrent)));
				sub(x(1), x(3), x(4));
			}
			loadByte(w(4), mem(25, offsetof(E, modeNext)));
			storeByte(w(4), mem(25, offsetof(E, modeCurrent)));
			store(x(3), mem(25, offsetof(E, cycleStartCurrent)));
			add(x(3), x(3), program.rawSingle() ? ERAM_COMMIT_STAGE_RAW_SINGLE : ERAM_COMMIT_STAGE);
			store(x(3), mem(25, offsetof(E, cycleCommit)));
			if (step.saveWriteSample || step.saveWritePacked)
				load(w(6), mem(25, offsetof(E, writeLatch)));
			if (step.saveWriteSample)
				store(w(6), mem(25, offsetof(E, writeLatchNext)));
			if (step.saveWritePacked)
			{
				packWriteLatchNative();
				storeHalf(w(7), mem(25, offsetof(E, writePackedNext)));
			}
			load(w(3), mem(25, offsetof(E, effectiveAddr)));
			load(w(4), mem(25, offsetof(E, immOffsetAccNext)));
			bitAnd(w(5), w(4), 0x3ffff);
			load(w(6), mem(25, offsetof(E, pos)));
			add(w(5), w(5), w(6));
			if (step.before.nextMode == 0x18)
			{
				a.mov(w(7), 0);
				for (unsigned bit = 0; bit < 20; bit += 5)
				{
					bits(w(8), w(4), bit, 1);
					negate(w(8), w(8));
					bitAnd(w(8), w(8), 31u << bit);
					bitOr(w(7), w(7), w(8));
				}
				bitClear(w(5), w(6), w(7));
				load(w(6), mem(25, offsetof(E, varOffsetNext)));
				add(w(5), w(5), w(6));
				bits(w(6), w(4), 1, 1);
				add(w(5), w(5), w(6));
				bitAnd(w(6), w(4), 0x100);
				store(w(6), mem(25, offsetof(E, highOffset)));
			}
			load(w(6), mem(25, offsetof(E, highOffset)));
			a.cmp(w(6), 0);
			a.mov(w(6), 0x40000);
			selectZero(w(6), w(6), Cond::Ne);
			add(w(5), w(5), w(6));
			bitAnd(w(5), w(5), ERAM_MASK_FULL);
			store(w(5), mem(25, offsetof(E, effectiveAddr)));
			if (step.transfer)
				a.mov(w(3), w(5));
			else
			{
				bitAnd(w(3), w(3), ~0x1ffu);
				bitAnd(w(6), w(5), 0x1ff);
				bitOr(w(3), w(3), w(6));
			}
			store(w(3), mem(25, offsetof(E, lowEffectiveAddr)));
			if (program.extended())
				extendedTransfer(step.addressExtendedMask, write);
			else if (program.rawSingle())
			{
				if (step.transfer)
				{
					load(w(5), mem(25, offsetof(E, effectiveAddr)));
					if (write)
					{
						load(w(6), mem(25, offsetof(E, writeLatchNext)));
						writePhaseNative(false);
					}
					else
						rawRead();
					signedBits(w(6), w(6), 0, 24);
					store(w(6), mem(25, offsetof(E, readLatch)));
				}
			}
			else if (program.compress)
			{
				if (write)
					loadHalf(w(7), mem(25, offsetof(E, writePackedNext)));
				else
				{
					load(w(5), mem(25, offsetof(E, lowEffectiveAddr)));
					readPackedNative();
					if (step.before.shortNext)
					{
						shr(w(7), w(7), 8);
						orShifted(w(7), w(7), w(7), 8);
					}
					storeHalf(w(7), mem(25, offsetof(E, readPackedNext)));
				}
				if (write || step.before.shortNext || step.earlyHold != 1)
				{
					loadHalf(w(8), mem(25, offsetof(E, readPacked)));
					Isa::insertByte(w(7), w(8), true, w(9));
					if (!write && !step.before.shortNext && step.earlyHold < 0)
					{
						// The incoming frame's timing selects a value, never a code path.
						a.cmp(x(1), ERAM_EARLY_READ_HOLD_GAP);
						select(w(7), w(8), w(7), Cond::Ls);
						storeHalf(w(7), mem(25, offsetof(E, readPacked)));
						if (step.readLatchStages & F::Address)
						{
							load(w(9), mem(25, offsetof(E, readLatch)));
							unpackEramNative();
							a.cmp(x(1), ERAM_EARLY_READ_HOLD_GAP);
							select(w(6), w(9), w(6), Cond::Ls);
							store(w(6), mem(25, offsetof(E, readLatch)));
						}
					}
					else
						setPackedNative(step.readLatchStages & F::Address);
				}
			}
			else
			{
				if (write)
					load(w(6), mem(25, offsetof(E, writeLatchNext)));
				else
				{
					load(w(5), mem(25, offsetof(E, lowEffectiveAddr)));
					rawRead();
				}
				store(w(6), mem(25, offsetof(E, readRawNext)));
				bitAnd(w(6), w(6), 0xfff);
				load(w(7), mem(25, offsetof(E, readLatch)));
				bitAnd(w(7), w(7), 0xfff000);
				bitOr(w(6), w(6), w(7));
				signedBits(w(6), w(6), 0, 24);
				store(w(6), mem(25, offsetof(E, readLatch)));
			}
			if (step.restart)
			{
				if (step.restartDeferred)
					loadByte(w(3), mem(25, offsetof(E, deferredCommand)));
				else
					a.mov(w(3), w(0));
				storeByte(w(3), mem(25, offsetof(E, modeNext)));
				sub(x(3), x(2), step.restartDeferred ? 1 : 0);
				store(x(3), mem(25, offsetof(E, cycleStartNext)));
				add(x(3), x(3), program.spacing);
				store(x(3), mem(25, offsetof(E, nextCommandCycle)));
				if (step.restartDeferred)
					store(w(0), mem(25, offsetof(E, immOffsetAccNext)));
				else
					storeZero(mem(25, offsetof(E, immOffsetAccNext)), 4);
				storeZero(mem(25, offsetof(E, varOffsetNext)), 4);
				storeZero(mem(25, offsetof(E, deferredCommand)), 1);
			}
		}

		// Slots 756..767: raw IRAM at 00/FF, then the retained source times the host-tail words.
		void emitTail(unsigned c, const CoreProgram::Node& node)
		{
			CoreProgram::State state = node.state;
			for (const auto& step : node.tail)
			{
				const unsigned t = step.clock - NORMAL_DECODE_END;
				if (N != 0 && c == 1)
					emitEram(step.eram, PRAM_SIZE);
				auto advance = a.newLabel();
				if (state.pc < PRAM_SIZE)
				{
					if (t < 2)
					{
						liveWord(state.pc, 7);
						if (t == 0)
							load(w(6), mem(22, 0));
						else
						{
							load(w(5), mem(19, offsetof(JitFrame, advanceMemory)));
							a.cmp(w(5), 0);
							a.mov(w(5), 255);
							selectZero(w(5), w(5), Cond::Ne);
							load(w(6), indexed(x(22), x(5), 2));
						}
						signedBits(w(6), w(6), 0, 24);
						store(w(6), core(offsetof(CoreRuntime, lastMulInputA)));
					}
					else
					{
						load(w(5), mem(19, offsetof(JitFrame, hostTail) + c * 4));
						jumpIfBit(w(5), 31, advance);
						add(w(6), w(5), (t - 2) / 2);
						a.mov(w(7), PRAM_SIZE - 1);
						a.cmp(w(6), w(7));
						select(w(7), w(6), w(7), Cond::Lo);
						bitAnd(w(6), w(6), 1023);
						a.cmp(w(5), PRAM_SIZE);
						select(w(5), w(7), w(6), Cond::Lo);
						load(w(7), indexed(x(28), x(5), 2));
						load(w(6), core(offsetof(CoreRuntime, lastMulInputA)));
					}
					multiplyByte(x(8), w(6), w(7));
					bits(w(7), w(7), 8, 2);
					shl(w(7), w(7), 2);
					a.mov(w(9), 0x3567);
					shr(w(7), w(9), w(7));
					bitAnd(w(7), w(7), 15);
					sar(x(8), x(8), x(7));
					add(w(26), w(26), w(8));
					signedBits(w(26), w(26), 0, 30);
				}
				a.bind(advance);
				samplePredicate(state.predicate.pending, 26);
				if (clocks - step.clock <= 3)
					for (unsigned b = 0; b < 2; ++b)
						store(w(b ? 27 : 26), core(acc(b) + offsetof(Accumulator, hist) + state.head * 4));
				state = step.after;
			}
			a.mov(w(5), state.pc);
			store(w(5), core(offsetof(CoreRuntime, pc)));
			storeZero(core(offsetof(CoreRuntime, lastMul30)), 1);
			a.mov(w(5), state.head);
			for (unsigned b = 0; b < 2; ++b)
			{
				store(w(b ? 27 : 26), core(acc(b)));
				store(w(5), core(acc(b) + offsetof(Accumulator, head)));
			}
			if (N != 0 && c == 1)
			{
				eramImmediate(state.controller);
				eramVariable(state.controller.variableMask);
			}
			savePredicate(c, state.predicate);
		}

		void publishCoefficient(unsigned c, const Instr& in)
		{
			if (!(in.coefWriteMask & (1 << c)))
				return;
			loadPipe(in.coefFromB, 0, true);
			store(w(0), mem(20, L::mulcoeffs + in.coefIndex * 4));
			publishDmacCoefficient(in.coefIndex, 0);
		}
		void savePredicate(unsigned c, const CoreProgram::Predicate& predicate)
		{
			if (!graphs[c].hasCmp)
				return;
			a.mov(w(0), predicate.pos);
			storeHalf(w(0), core(offsetof(CoreRuntime, skipfieldPos)));
			a.mov(w(0), predicate.neg);
			storeHalf(w(0), core(offsetof(CoreRuntime, skipfieldNeg)));
			a.mov(w(0), predicate.pending >= 0);
			storeByte(w(0), core(offsetof(CoreRuntime, comparePending)));
			unsigned ready = 0;
			a.mov(w(0), 0);
			for (unsigned k = 0; k < 3; ++k)
			{
				if (predicate.ready[k] < 0)
					continue;
				ready |= 1u << k;
				loadByte(w(1), predicateMemory(predicate.ready[k]));
				orShifted(w(0), w(0), w(1), k);
			}
			storeByte(w(0), core(offsetof(CoreRuntime, predicateValues)));
			a.mov(w(0), ready);
			storeByte(w(0), core(offsetof(CoreRuntime, predicateReady)));
			if (predicate.govern >= 0)
			{
				loadByte(w(0), predicateMemory(predicate.govern));
				storeByte(w(0), core(offsetof(CoreRuntime, compareNegative)));
			}
		}
		// lastMul30 and the transform are optimizer facts; an undefined one keeps the incoming value.
		void saveCore(const CoreProgram::State& state)
		{
			if (state.lastMul30 >= 0)
			{
				a.mov(w(0), state.lastMul30);
				storeByte(w(0), core(offsetof(CoreRuntime, lastMul30)));
			}
			if (state.lastTransform >= 0)
			{
				a.mov(w(0), state.lastTransform);
				storeByte(w(0), core(offsetof(CoreRuntime, lastCoefficientTransform)));
			}
			const unsigned head = state.head;
			store(w(29), core(offsetof(CoreRuntime, lastMulInputA)));
			a.mov(w(1), head);
			for (unsigned b = 0; b < 2; ++b)
			{
				for (unsigned k = 0; k < 3; ++k)
					store(w((b ? 15 : 12) + k), core(acc(b) + offsetof(Accumulator, hist) + ((head + k) % 3) * 4));
				store(w(b ? 27 : 26), core(acc(b)));
				store(w(1), core(acc(b) + offsetof(Accumulator, head)));
			}
		}

		void emitCore(unsigned c)
		{
			const auto& graph = graphs[c];
			const bool needsClock = arbitration || graph.needsClock;
			cacheWritePacked = !needsClock;
			writePackedValid = false;
			std::vector<asmjit::Label> labels(graph.blocks.size()), exits(graph.nodes.size());
			for (auto& label : labels)
				label = a.newLabel();
			for (auto& label : exits)
				label = a.newLabel();
			auto finish = a.newLabel();
			Isa::enterCore(c, offsetof(R, core) + c * sizeof(CoreRuntime));
			load(x(22), mem(19, offsetof(JitFrame, iram) + c * sizeof(void*)));
			load(x(23), mem(19, offsetof(JitFrame, coef16) + c * sizeof(void*)));
			for (unsigned b = 0; b < 2; ++b)
			{
				for (unsigned k = 0; k < 3; ++k)
					if (graph.nodes[0].liveIn & (1u << (b * 4 + k)))
						load(w((b ? 15 : 12) + k), core(acc(b) + offsetof(Accumulator, hist) + k * 4));
				if (graph.nodes[0].liveIn & (8u << (b * 4)))
					load(w(b ? 27 : 26), core(acc(b)));
			}
			bool needsWords = clocks > NORMAL_DECODE_END;
			for (const auto& node : graph.nodes)
			{
				const auto& e = node.eram;
				needsWords |= e.start || e.defer || e.restart ||
					(e.before.immediateKnown && e.before.immediateTable < 0) ||
					(node.state.controller.immediateKnown && node.state.controller.immediateTable < 0);
				if (!node.idle)
				{
					const auto& in = program.instruction(c, node.pc);
					needsWords |= !node.deadMac && in.control && in.sub == 6;
				}
			}
			if (needsWords)
				load(x(28), mem(19, offsetof(JitFrame, words) + c * sizeof(void*)));
			if (graph.nodes[0].liveIn & 0x100)
				load(w(29), core(offsetof(CoreRuntime, lastMulInputA)));
			if (needsClock)
				a.mov(w(21), 0);
			jump(labels[graph.owner[0]]);
			computeBlockFacts(c);
			for (unsigned bi = 0; bi < graph.blocks.size(); ++bi)
			{
				Isa::beginBlock();
				beginBlockFacts(bi);
				const auto& block = graph.blocks[bi];
				writePackedValid = false;
				const auto& first = graph.nodes[block.ops.front().node];
				a.bind(labels[bi]);
				if (first.times[graph.clocks])
				{
					if (first.times.count() == 1)
					{
						jump(exits[block.ops.front().node]);
						continue;
					}
					jumpIfEqual(w(21), graph.clocks, exits[block.ops.front().node]);
				}
				if (first.idle)
				{
					if (N != 0 && c == 1)
						emitEram(first.eram, PRAM_SIZE);
					if (needsClock)
						add(w(21), w(21), 1);
					if (first.next >= 0)
						jump(labels[graph.owner[first.next]]);
					continue;
				}
				unsigned elapsed = 0, idleEram = 0;
				auto flushClock = [&]
				{
					if (elapsed && needsClock)
					{
						add(w(21), w(21), elapsed);
						elapsed = 0;
					}
				};
				auto stall = a.newLabel();
				for (const auto& op : block.ops)
				{
					Isa::beginOperation();
					operation = &op;
					const auto& node = graph.nodes[op.node];
					const auto& in = program.instruction(c, node.pc);
					publishCoefficient(c, in);
					if (N != 0 && c == 1)
					{
						if (!node.eram.empty())
							emitEram(node.eram, node.state.pc, idleEram, false);
						++idleEram;
					}
					if (arbitration && in.requestsGram)
					{
						flushClock();
						add(x(10), x(19), offsetof(KernelFrame, requests));
						if (c == 1)
						{
							a.mov(w(0), 1);
							storeByte(w(0), indexed(x(10), x(21), 0));
						}
						else if (node.stall >= 0)
						{
							loadByte(w(0), indexed(x(10), x(21), 0));
							jumpNonzero(w(0), stall);
						}
					}
					emitArithmetic(c, node.pc);
					++elapsed;
				}
				flushClock();
				if (idleEram)
				{
					load(x(0), mem(20, L::cycle));
					add(x(0), x(0), idleEram);
					store(x(0), mem(20, L::cycle));
				}
				Isa::normalize(block, blockWritten);
				const auto& last = graph.nodes[block.ops.back().node];
				if (last.branch)
				{
					auto notTaken = a.newLabel();
					if (last.conditional)
						jumpZero(w(4), notTaken);
					a.mov(w(0), graph.nodes[last.taken].state.target);
					store(w(0), core(offsetof(CoreRuntime, pcjumpto)));
					jump(labels[graph.owner[last.taken]]);
					if (last.conditional)
					{
						a.bind(notTaken);
						jump(labels[graph.owner[last.next]]);
					}
				}
				else if (last.next >= 0)
					jump(labels[graph.owner[last.next]]);
				if (first.stall >= 0)
				{
					a.bind(stall);
					samplePredicate(first.predicate.pending, 26);
					for (unsigned b = 0; b < 2; ++b)
					{
						const unsigned r = b ? 15 : 12;
						a.mov(w(r), w(r + 1));
						a.mov(w(r + 1), w(r + 2));
						a.mov(w(r + 2), w(b ? 27 : 26));
					}
					if (needsClock)
						add(w(21), w(21), 1);
					jump(labels[graph.owner[first.stall]]);
				}
			}
			// The exit table of a large graph can span more than a conditional branch reaches:
			// park the pending cold code first (the last block ended with a jump).
			Isa::parkColdCode();
			for (unsigned id = 0; id < graph.nodes.size(); ++id)
			{
				const auto& node = graph.nodes[id];
				if (!node.times[graph.clocks])
					continue;
				a.bind(exits[id]);
				writePackedValid = false;
				a.mov(w(0), node.state.pc);
				store(w(0), core(offsetof(CoreRuntime, pc)));
				a.mov(w(0), node.state.at);
				store(w(0), core(offsetof(CoreRuntime, pcjumpat)));
				savePredicate(c, node.predicate);
				saveCore(node.state);
				if (N != 0 && c == 1)
				{
					eramImmediate(node.state.controller);
					eramVariable(node.state.controller.variableMask);
				}
				operation = nullptr;
				if (clocks > NORMAL_DECODE_END)
					emitTail(c, node);
				jump(finish);
			}
			a.bind(finish);
		}

		void emit()
		{
			graphs[1] = CoreProgram::build(program, 1, clocks, {}, N != 0);
			graphs[0] = CoreProgram::build(program, 0, clocks, graphs[1].gramCycles);
			for (const auto& node : graphs[0].nodes)
				arbitration |= node.stall >= 0;
			if (graphs[0].hasCmp || graphs[1].hasCmp)
				predicateStack = 16;
			for (unsigned c = 0; c < 2; ++c)
				for (const auto& node : graphs[c].nodes)
					if (node.sample >= 0)
						predicateStack = std::max(predicateStack, (unsigned(node.sample) + 16) & ~15u);
			Isa::prologue(predicateStack);
			load(x(20), mem(19, offsetof(JitFrame, runtime)));
			if (N != 0)
			{
				Isa::setEramBase(offsetof(R, eram));
				load(x(30), mem(19, offsetof(JitFrame, eramPacked)));
				Isa::loadEramMemory(offsetof(JitFrame, eramMemory));
			}
			if (arbitration)
				Isa::clearRequests(offsetof(KernelFrame, requests));
			emitCore(1);
			emitCore(0);
			Isa::epilogue(predicateStack);
			// Rarely taken paths (the saturation clamps) live past the return, reached and left
			// by branches, so the hot code keeps its branches not taken.
			Isa::emitColdCode();
			Isa::finishFunction();
		}

	public:
		static KernelFn compile(asmjit::JitRuntime& runtime, ProgramSnapshot program, unsigned clocks)
		{
			if (clocks > FRAME_CLOCKS)
				return nullptr;
			program.decode();
			asmjit::CodeHolder code;
			code.init(runtime.environment());
			EmitErrors errors;
			code.setErrorHandler(&errors);
			// Diagnostics: with ESP_JIT_DUMP=<directory> in the environment, every kernel's
			// listing (the allocated code on x86) is written there, numbered in compile order.
			asmjit::FileLogger logger;
			FILE* dumpFile = nullptr;
			if (const char* dir = std::getenv("ESP_JIT_DUMP"))
			{
				static std::atomic<unsigned> serial{0};
				char name[1024];
				std::snprintf(name, sizeof(name), "%s/kernel_%03u_clocks%u.asm", dir, serial++, clocks);
				dumpFile = std::fopen(name, "w");
				if (dumpFile)
				{
					logger.setFile(dumpFile);
					code.setLogger(&logger);
				}
			}
			typename Isa::Assembler assembler(&code);
			JitEmitter emitter(assembler, program, clocks);
			emitter.emit();
			auto fn = Isa::finish(runtime, code, assembler, errors.failed);
			if (dumpFile)
				std::fclose(dumpFile);
			return fn;
		}
	};
}
#endif
