#pragma once

#include "esp_jit_emitter.h"

#if CHIPS_JIT_X86_64
#include <asmjit/x86.h>
#include <type_traits>

namespace esp
{
	// AsmJit's register allocator places the pipeline values and temporaries. The arm64
	// register numbers name virtual registers; inside a block each accumulator result
	// gets its own virtual, edges use fixed ones.
	template <int N>
	class X86Isa
	{
	protected:
		static constexpr int lg2eramSize = N;
		using Assembler = asmjit::x86::Compiler;
		using Gp = asmjit::x86::Gp;
		using Mem = asmjit::x86::Mem;
		using CC = asmjit::x86::CondCode;
		using R = Runtime<N>;

		Assembler& a;
		unsigned currentCore = 0;
		// The arm64 map's 31 registers, plus 31: the ERAM memory base (x86 has the virtual to spare).
		Gp values[32];
		Gp entryValues[32];
		Mem predicates;

		explicit X86Isa(Assembler& _a) : a(_a)
		{
			for (unsigned i = 0; i < 32; ++i)
				entryValues[i] = values[i] = a.newUInt64();
		}

		void restoreEntryValues()
		{
			for (unsigned i : {12u, 13u, 14u, 15u, 16u, 17u, 26u, 27u, 29u})
				values[i] = entryValues[i];
		}

		Gp x(unsigned n) const { return values[n]; }
		Gp w(unsigned n) const { return values[n].r32(); }
		// Core and ERAM state are addressed from the runtime base, keeping two pointers free.
		Mem mem(unsigned base, size_t offset) const
		{
			if (base == 24)
				return mem(20, offsetof(R, core) + currentCore * sizeof(CoreRuntime) + offset);
			if (base == 25)
				return mem(20, offsetof(R, eram) + offset);
			return asmjit::x86::ptr(x(base), int32_t(offset));
		}
		Mem core(size_t offset) const { return mem(24, offset); }
		static Mem indexed(const Gp& base, const Gp& index, unsigned shift) { return asmjit::x86::ptr(base, index, shift); }
		Mem predicateMemory(int offset) const { return predicates.cloneAdjusted(offset); }
		static CC cc(Cond c)
		{
			switch (c)
			{
			case Cond::Eq: return CC::kE;
			case Cond::Ne: return CC::kNE;
			case Cond::Lt: return CC::kL;
			case Cond::Ge: return CC::kGE;
			case Cond::Lo: return CC::kB;
			case Cond::Ls: return CC::kBE;
			}
			return CC::kE;
		}

		void move(const Gp& d, const Gp& s)
		{
			if (d != s)
				a.mov(d, s);
		}
		Gp temporary(const Gp& like) { return a.newGp(like.size() == 8 ? asmjit::TypeId::kUInt64 : asmjit::TypeId::kUInt32); }
		template <class T> void binary(asmjit::InstId id, const Gp& d, const Gp& l, const T& r)
		{
			if constexpr (std::is_same_v<T, Gp>)
			{
				if (d == r && d != l)
				{
					if (id == asmjit::x86::Inst::kIdAdd || id == asmjit::x86::Inst::kIdAnd ||
						id == asmjit::x86::Inst::kIdOr || id == asmjit::x86::Inst::kIdXor)
					{
						a.emit(id, d, l);
						return;
					}
					auto saved = temporary(d);
					a.mov(saved, r);
					move(d, l);
					a.emit(id, d, saved);
					return;
				}
			}
			move(d, l);
			a.emit(id, d, r);
		}
		// Three-operand forms without a copy where baseline x86-64 has one: LEA adds, IMUL by a
		// power of two shifts left, and both compute into a fresh destination.
		template <class T> void add(const Gp& d, const Gp& l, const T& r)
		{
			if (d != l)
			{
				if constexpr (std::is_same_v<T, Gp>)
				{
					if (d != r)
					{
						a.lea(d, asmjit::x86::ptr(l.r64(), r.r64()));
						return;
					}
				}
				else
				{
					a.lea(d, asmjit::x86::ptr(l.r64(), int32_t(r)));
					return;
				}
			}
			binary(asmjit::x86::Inst::kIdAdd, d, l, r);
		}
		template <class T> void sub(const Gp& d, const Gp& l, const T& r)
		{
			if constexpr (!std::is_same_v<T, Gp>)
			{
				if (d != l)
				{
					a.lea(d, asmjit::x86::ptr(l.r64(), -int32_t(r)));
					return;
				}
			}
			binary(asmjit::x86::Inst::kIdSub, d, l, r);
		}
		template <class T> void bitAnd(const Gp& d, const Gp& l, const T& r) { binary(asmjit::x86::Inst::kIdAnd, d, l, r); }
		template <class T> void bitOr(const Gp& d, const Gp& l, const T& r) { binary(asmjit::x86::Inst::kIdOr, d, l, r); }
		template <class T> void bitXor(const Gp& d, const Gp& l, const T& r) { binary(asmjit::x86::Inst::kIdXor, d, l, r); }
		void bitClear(const Gp& d, const Gp& l, const Gp& r)
		{
			auto mask = temporary(d);
			a.mov(mask, r);
			a.not_(mask);
			bitAnd(d, l, mask);
		}
		template <class T> void shift(asmjit::InstId id, const Gp& d, const Gp& l, const T& r)
		{
			if constexpr (std::is_same_v<T, Gp>)
			{
				// Variable shifts count in CL; the allocator places the count there. Only a
				// count that is the destination itself needs saving first.
				if (d == r)
				{
					auto count = a.newUInt32();
					a.mov(count, r.r32());
					move(d, l);
					a.emit(id, d, count.r8());
					return;
				}
				move(d, l);
				a.emit(id, d, r.r8());
			}
			else
			{
				move(d, l);
				if (r)
					a.emit(id, d, r);
			}
		}
		template <class T> void shl(const Gp& d, const Gp& l, const T& r)
		{
			if constexpr (!std::is_same_v<T, Gp>)
			{
				if (d != l && r > 0 && r < 32)
				{
					a.imul(d, l, int32_t(1u << r));
					return;
				}
			}
			shift(asmjit::x86::Inst::kIdShl, d, l, r);
		}
		template <class T> void shr(const Gp& d, const Gp& l, const T& r) { shift(asmjit::x86::Inst::kIdShr, d, l, r); }
		template <class T> void sar(const Gp& d, const Gp& l, const T& r) { shift(asmjit::x86::Inst::kIdSar, d, l, r); }
		void orShifted(const Gp& d, const Gp& l, const Gp& r, unsigned amount)
		{
			auto shifted = temporary(d);
			shl(shifted, r, amount);
			bitOr(d, l, shifted);
		}
		void addShifted(const Gp& d, const Gp& l, const Gp& r, unsigned amount)
		{
			if (amount <= 3)
			{
				a.lea(d, asmjit::x86::ptr(l.r64(), r.r64(), amount));
				return;
			}
			auto shifted = temporary(d);
			shl(shifted, r, amount);
			add(d, l, shifted);
		}
		void invert(const Gp& d, const Gp& s)
		{
			move(d, s);
			a.not_(d);
		}
		void negate(const Gp& d, const Gp& s)
		{
			move(d, s);
			a.neg(d);
		}
		void signedBits(const Gp& d, const Gp& s, unsigned start, unsigned width)
		{
			shl(d, s, 32 - start - width); // an IMUL into d when d is not s
			a.sar(d, 32 - width);
		}
		void bits(const Gp& d, const Gp& s, unsigned start, unsigned width)
		{
			shr(d, s, start);
			a.and_(d, (1u << width) - 1);
		}
		void multiplyByte(const Gp& d, const Gp& l, const Gp& r)
		{
			auto rhs = a.newInt64();
			a.movsx(rhs, r.r8());
			a.movsxd(d, l);
			a.imul(d, rhs);
		}
		// The signed byte at bit `start` of s, ready for a product: sign-extended to 64 bits.
		void signedByte(const Gp& d, const Gp& s, unsigned start)
		{
			if (start)
			{
				move(d.r32(), s.r32());
				a.sar(d.r32(), start);
				a.movsx(d.r64(), d.r8());
				return;
			}
			a.movsx(d.r64(), s.r8());
		}
		// d = 4 + 2 * exponent of the packed code s (its bits 15:14).
		void exponentShift(const Gp& d, const Gp& s)
		{
			shr(d, s, 13);
			a.and_(d, 6);
			a.lea(d, asmjit::x86::ptr(d.r64(), 4));
		}
		// The ERAM sample memory's base lives in a virtual register for the whole kernel.
		void loadEramMemory(size_t frameOffset)
		{
			values[31] = a.newUInt64();
			load(x(31), mem(19, frameOffset));
		}
		Gp eramMemory() { return x(31); }
		// Byte `high` of d := byte `high` of s; the rest of d stays.
		void insertByte(const Gp& d, const Gp& s, bool high, const Gp& scratch)
		{
			if (!high)
			{
				a.mov(d.r8(), s.r8());
				return;
			}
			a.mov(scratch, s);
			a.and_(scratch, 0xff00);
			a.and_(d, 0xffff00ffu);
			a.or_(d, scratch);
		}
		void select(const Gp& d, const Gp& yes, const Gp& no, Cond c)
		{
			if (d == yes)
			{
				if (yes != no)
					a.cmov(asmjit::x86::negateCond(cc(c)), d, no);
				return;
			}
			move(d, no);
			a.cmov(cc(c), d, yes);
		}
		void selectZero(const Gp& d, const Gp& yes, Cond c)
		{
			auto zero = temporary(d);
			a.mov(zero, 0); // MOV preserves the flags.
			select(d, yes, zero, c);
		}
		void setCondition(const Gp& d, Cond c)
		{
			a.set(cc(c), d.r8());
			a.movzx(d, d.r8());
		}
		void jump(const asmjit::Label& l) { a.jmp(l); }
		void jumpZero(const Gp& r, const asmjit::Label& l)
		{
			a.test(r, r);
			a.jz(l);
		}
		void jumpNonzero(const Gp& r, const asmjit::Label& l)
		{
			a.test(r, r);
			a.jnz(l);
		}
		void jumpIfBit(const Gp& r, unsigned bit, const asmjit::Label& l)
		{
			a.test(r, 1u << bit);
			a.jnz(l);
		}
		void jumpIfEqual(const Gp& r, unsigned imm, const asmjit::Label& l)
		{
			a.cmp(r, imm);
			a.je(l);
		}
		void load(const Gp& d, Mem m)
		{
			m.setSize(d.size());
			a.mov(d, m);
		}
		void loadByte(const Gp& d, Mem m)
		{
			m.setSize(1);
			a.movzx(d, m);
		}
		void loadHalf(const Gp& d, Mem m)
		{
			m.setSize(2);
			a.movzx(d, m);
		}
		void loadSignedHalf(const Gp& d, Mem m)
		{
			m.setSize(2);
			a.movsx(d, m);
		}
		void store(const Gp& s, Mem m)
		{
			m.setSize(s.size());
			a.mov(m, s);
		}
		void storeByte(const Gp& s, Mem m)
		{
			m.setSize(1);
			a.mov(m, s.r8());
		}
		void storeHalf(const Gp& s, Mem m)
		{
			m.setSize(2);
			a.mov(m, s.r16());
		}
		void storeZero(Mem m, unsigned size)
		{
			m.setSize(size);
			a.mov(m, 0);
		}

		// Clamp to signed 24 bits; most samples need no clamping. The clamp stays inline: moved
		// out of line, every clamped virtual register would be live up to the stub and the
		// allocator would spill around hundreds of function-long ranges.
		void saturate(unsigned reg)
		{
			auto shifted = a.newUInt32();
			auto done = a.newLabel();
			a.lea(shifted, asmjit::x86::ptr(x(reg), 0x800000));
			a.cmp(shifted, 0xffffff);
			a.jbe(done);
			a.sar(w(reg), 31);
			a.xor_(w(reg), 0x7fffff);
			a.bind(done);
		}
		void emitColdCode() {}
		void parkColdCode() {}
		void finishFunction() { a.endFunc(); }
		// w6 signed sample -> w7 packed code (baseline x86-64: BSR, no LZCNT). The exponent is
		// the magnitude's bit count above 17 in pairs: bsr - 15, clamped at zero, low bit dropped.
		void packEramNative()
		{
			signedBits(w(6), w(6), 0, 24);
			sar(w(8), w(6), 31);
			a.xor_(w(8), w(6));
			a.or_(w(8), 1); // BSR of zero is undefined; bit 0 never reaches a threshold
			a.bsr(w(8), w(8));
			a.sub(w(8), 15);
			a.mov(w(9), 0); // MOV preserves the flags of the subtraction.
			a.cmov(CC::kGE, w(9), w(8));
			a.and_(w(9), 6);
			a.lea(w(8), asmjit::x86::ptr(x(9), 4));
			sar(w(7), w(6), w(8));
			a.and_(w(7), 0x3fff);
			a.shl(w(9), 13);
			a.or_(w(7), w(9));
		}

		void enterCore(unsigned c, size_t)
		{
			currentCore = c;
			restoreEntryValues();
		}
		void beginBlock() { restoreEntryValues(); }
		void beginOperation()
		{
			for (unsigned i = 0; i < 12; ++i)
				values[i] = a.newUInt64();
		}
		void setLastInput()
		{
			values[29] = a.newUInt64();
			a.mov(w(29), w(0));
		}
		// The retained multiplicand is about to be written in place (load, constant or shift).
		void beginLastInput() { values[29] = a.newUInt64(); }
		void shiftLastInput(unsigned amount)
		{
			const auto previous = values[29];
			values[29] = a.newUInt64();
			sar(w(29), previous.r32(), amount);
		}

		// The operand is in w(operandReg), a 24-bit value when operand24; the MUL30/DMAC
		// coefficient in x1 is sign-extended already. The result is named, not copied into a
		// recycled pipeline register.
		void emitProduct(const CoreProgram::Operation& op, const Instr& in, unsigned pc, bool specialCoef,
			unsigned operandReg, bool operand24)
		{
			const auto previous = w(op.current[in.accB]);
			if (op.constantShift >= 0)
			{
				loadSignedHalf(w(2), mem(23, pc * 2));
				const int amount = op.constantShift - 7;
				if (amount < 0)
					a.sar(w(2), -amount);
				else if (amount)
					a.shl(w(2), amount);
			}
			else
			{
				if (operandReg == 0)
				{
					values[2] = values[0]; // the product consumes the operand register
					if (operand24)
						a.movsxd(x(2), w(2));
				}
				else
					a.movsxd(x(2), w(operandReg)); // the retained input stays intact
				if (!operand24)
				{
					a.shl(x(2), 40);
					a.sar(x(2), 40);
				}
				if (specialCoef)
				{
					a.imul(x(2), x(1));
					loadByte(w(3), mem(23, offsetof(LiveTables, shift) + pc));
					sar(x(2), x(2), x(3));
				}
				else
				{
					a.movsx(x(1), mem(23, pc * 2).cloneResized(2));
					a.imul(x(2), x(1));
					a.sar(x(2), 7);
				}
			}
			if (!in.replace)
				a.add(w(2), previous);
			values[op.destination] = values[2];
			values[0] = a.newUInt64(); // the predicate sample must not overwrite the product
		}

		// Capture every source before assigning the edge registers: an edge can permute them. A
		// value a product wrote in this block is wrapped to 30 bits; the others arrived wrapped
		// and are passed through, so an unchanged slot costs nothing.
		void normalize(const CoreProgram::Block& block, unsigned written)
		{
			Gp outputs[8];
			for (unsigned b = 0; b < 2; ++b)
				for (unsigned k = 0; k < 4; ++k)
					if (block.liveOut & (1u << (b * 4 + k)))
					{
						const unsigned source = k == 3 ? block.current[b] : block.history[b][k];
						if (!(written & (1u << source)))
						{
							outputs[b * 4 + k] = values[source].r32();
							continue;
						}
						outputs[b * 4 + k] = a.newUInt32();
						signedBits(outputs[b * 4 + k], w(source), 0, 30);
					}
			for (unsigned b = 0; b < 2; ++b)
				for (unsigned k = 0; k < 4; ++k)
					if (block.liveOut & (1u << (b * 4 + k)))
						move(entryValues[k == 3 ? (b ? 27 : 26) : (b ? 15 : 12) + k].r32(), outputs[b * 4 + k]);
			if (block.liveOut & 0x100)
				move(entryValues[29].r32(), w(29));
			restoreEntryValues();
		}

		void prologue(unsigned predicateStack)
		{
			auto function = a.addFunc(asmjit::FuncSignatureT<void, KernelFrame*>());
			function->setArg(0, x(19));
			if (predicateStack)
				predicates = a.newStack(predicateStack, 16);
		}
		void setEramBase(size_t) {}
		void clearRequests(size_t offset)
		{
			add(x(10), x(19), offset);
			for (unsigned i = 0; i < FRAME_CLOCKS; i += 8)
				storeZero(mem(10, i), 8);
		}
		void epilogue(unsigned) { a.ret(); }

		// Rejects calls and jumps to anything but a local label. Instruction nodes are
		// inspected, not bytes: immediates can contain call opcodes.
		static KernelFn finish(asmjit::JitRuntime& runtime, asmjit::CodeHolder& code, Assembler& assembler, bool failed)
		{
			if (assembler.finalize() != asmjit::kErrorOk)
				return nullptr;
			for (auto* node = assembler.firstNode(); node; node = node->next())
			{
				if (node->isInvoke())
					return nullptr;
				if (node->type() != asmjit::NodeType::kInst && node->type() != asmjit::NodeType::kJump)
					continue;
				const auto* instruction = node->as<asmjit::InstNode>();
				const auto flow = asmjit::x86::InstDB::infoById(instruction->id()).controlFlow();
				if (flow == asmjit::InstControlFlow::kCall)
					return nullptr;
				if ((flow == asmjit::InstControlFlow::kJump || flow == asmjit::InstControlFlow::kBranch) &&
					!instruction->op(0).isLabel())
					return nullptr;
			}
			if (failed)
				return nullptr;
			KernelFn fn = nullptr;
			if (runtime.add(&fn, &code) != asmjit::kErrorOk)
				return nullptr;
			return fn;
		}
	};

	template <int N> using JitX86 = JitEmitter<X86Isa<N>>;
}
#endif
