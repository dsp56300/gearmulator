#pragma once

#include "esp_jit_emitter.h"

#if CHIPS_JIT_ARM64
#include <asmjit/a64.h>

namespace esp
{
	// Fixed register map (see JitEmitter); x18 is left to the platform.
	template <int N>
	class Arm64Isa
	{
	protected:
		static constexpr int lg2eramSize = N;
		using Assembler = asmjit::a64::Assembler;
		using Gp = asmjit::a64::Gp;
		using Mem = asmjit::a64::Mem;
		using CondCode = asmjit::a64::CondCode;
		using L = RuntimeLayout<N>;

		// The assembler does not split an offset the load/store immediate cannot hold: such an
		// encoding fails the whole kernel, silently, and every frame falls back to the interpreter.
		// The unsigned immediate is 12 bits scaled by the access size, so the farthest field of each
		// width reached from x20 (runtime), x24 (core) and x25 (ERAM state) is pinned here.
		static_assert(offsetof(CoreRuntime, irq1Sign) < 4096, "byte fields of CoreRuntime out of strb range");
		static_assert(offsetof(CoreRuntime, skipfieldNeg) < 8192, "halfword fields of CoreRuntime out of strh range");
		static_assert(offsetof(CoreRuntime, lastMulInputA) < 16384, "word fields of CoreRuntime out of str range");
		static_assert(L::mulcoeffs + 7 * 4 < 16384 && L::varOffset < 16384 && L::readLatch < 16384 &&
			L::writeLatch < 16384 && L::mailbox < 16384, "runtime word fields out of str range from x20");
		static_assert(L::cycle < 32768, "ERAM cycle out of str range from x20");
		static_assert(offsetof(EramRuntime<N>, deferredCommand) < 4096, "byte fields of EramRuntime out of strb range");
		static_assert(offsetof(EramRuntime<N>, writePackedNext) < 8192, "halfword fields of EramRuntime out of strh range");
		static_assert(offsetof(EramRuntime<N>, readRawNext) < 16384, "word fields of EramRuntime out of str range");
		static_assert(offsetof(EramRuntime<N>, nextCommandCycle) < 32768, "doubleword fields of EramRuntime out of str range");
		static_assert(offsetof(JitFrame, advanceMemory) < 16384 && offsetof(KernelFrame, requests) < 4096, "frame out of range");
		static_assert(offsetof(LiveTables, shift) + PRAM_WORDS <= 4096, "shift table out of ldrb range from the coefficient table");
		static_assert(offsetof(Runtime<N>, gram) + IRAM_SIZE * 4 < 16384, "GRAM out of str range from a core's IRAM base");
		static_assert(FRAME_CLOCKS % 16 == 0, "clearRequests stores request bytes in pairs of doublewords");

		Assembler& a;

		explicit Arm64Isa(Assembler& _a) : a(_a) {}

		static Gp x(unsigned n) { return asmjit::a64::GpX(n); }
		static Gp w(unsigned n) { return asmjit::a64::GpW(n); }
		static Mem mem(unsigned base, size_t offset) { return asmjit::a64::ptr(x(base), int32_t(offset)); }
		static Mem core(size_t offset) { return mem(24, offset); }
		static Mem indexed(const Gp& base, const Gp& index, unsigned shift)
		{
			return shift ? asmjit::a64::ptr(base, index, asmjit::a64::lsl(shift)) : asmjit::a64::ptr(base, index);
		}
		static Mem predicateMemory(int offset) { return asmjit::a64::ptr(asmjit::a64::sp, offset); }
		static Gp zero(const Gp& like) { return like.isGpW() ? Gp(asmjit::a64::wzr) : Gp(asmjit::a64::xzr); }
		static CondCode cc(Cond c)
		{
			switch (c)
			{
			case Cond::Eq: return CondCode::kEQ;
			case Cond::Ne: return CondCode::kNE;
			case Cond::Lt: return CondCode::kLT;
			case Cond::Ge: return CondCode::kGE;
			case Cond::Lo: return CondCode::kLO;
			case Cond::Ls: return CondCode::kLS;
			}
			return CondCode::kAL;
		}

		void load(const Gp& d, const Mem& m) { a.ldr(d, m); }
		void loadByte(const Gp& d, const Mem& m) { a.ldrb(d, m); }
		void loadHalf(const Gp& d, const Mem& m) { a.ldrh(d, m); }
		void loadSignedHalf(const Gp& d, const Mem& m) { a.ldrsh(d, m); }
		void store(const Gp& s, const Mem& m) { a.str(s, m); }
		void storeByte(const Gp& s, const Mem& m) { a.strb(s, m); }
		void storeHalf(const Gp& s, const Mem& m) { a.strh(s, m); }
		void storeZero(const Mem& m, unsigned size)
		{
			if (size == 1) a.strb(asmjit::a64::wzr, m);
			else if (size == 2) a.strh(asmjit::a64::wzr, m);
			else if (size == 4) a.str(asmjit::a64::wzr, m);
			else a.str(asmjit::a64::xzr, m);
		}
		template <class T> void add(const Gp& d, const Gp& l, const T& r) { a.add(d, l, r); }
		template <class T> void sub(const Gp& d, const Gp& l, const T& r) { a.sub(d, l, r); }
		template <class T> void bitAnd(const Gp& d, const Gp& l, const T& r) { a.and_(d, l, r); }
		template <class T> void bitOr(const Gp& d, const Gp& l, const T& r) { a.orr(d, l, r); }
		template <class T> void bitXor(const Gp& d, const Gp& l, const T& r) { a.eor(d, l, r); }
		void bitClear(const Gp& d, const Gp& l, const Gp& r) { a.bic(d, l, r); }
		template <class T> void shl(const Gp& d, const Gp& l, const T& r) { a.lsl(d, l, r); }
		template <class T> void shr(const Gp& d, const Gp& l, const T& r) { a.lsr(d, l, r); }
		template <class T> void sar(const Gp& d, const Gp& l, const T& r) { a.asr(d, l, r); }
		void orShifted(const Gp& d, const Gp& l, const Gp& r, unsigned shift) { a.orr(d, l, r, asmjit::a64::lsl(shift)); }
		void addShifted(const Gp& d, const Gp& l, const Gp& r, unsigned shift) { a.add(d, l, r, asmjit::a64::lsl(shift)); }
		void signedBits(const Gp& d, const Gp& s, unsigned start, unsigned width) { a.sbfx(d, s, start, width); }
		void bits(const Gp& d, const Gp& s, unsigned start, unsigned width) { a.ubfx(d, s, start, width); }
		void invert(const Gp& d, const Gp& s) { a.mvn(d, s); }
		void negate(const Gp& d, const Gp& s) { a.neg(d, s); }
		void select(const Gp& d, const Gp& yes, const Gp& no, Cond c) { a.csel(d, yes, no, cc(c)); }
		void selectZero(const Gp& d, const Gp& yes, Cond c) { a.csel(d, yes, zero(d), cc(c)); }
		void setCondition(const Gp& d, Cond c) { a.cset(d, cc(c)); }
		void jump(const asmjit::Label& l) { a.b(l); }
		void jumpZero(const Gp& r, const asmjit::Label& l) { a.cbz(r, l); }
		void jumpNonzero(const Gp& r, const asmjit::Label& l) { a.cbnz(r, l); }
		void jumpIfBit(const Gp& r, unsigned bit, const asmjit::Label& l) { a.tbnz(r, bit, l); }
		// B.cond reaches +/-1 MiB only; exit tables of large graphs can lie further.
		void jumpIfEqual(const Gp& r, unsigned imm, const asmjit::Label& l)
		{
			auto skip = a.newLabel();
			a.cmp(r, imm);
			a.b(CondCode::kNE, skip);
			a.b(l);
			a.bind(skip);
		}
		void multiplyByte(const Gp& d, const Gp& l, const Gp& r)
		{
			a.sbfx(d.w(), r, 0, 8);
			a.smull(d, l, d.w());
		}
		// The signed byte at bit `start` of s, ready for a product.
		void signedByte(const Gp& d, const Gp& s, unsigned start) { a.sbfx(d, s, start, 8); }
		// d = 4 + 2 * exponent of the packed code s (its bits 15:14).
		void exponentShift(const Gp& d, const Gp& s)
		{
			a.lsr(d, s, 13);
			a.and_(d, d, 6);
			a.add(d, d, 4);
		}
		// The ERAM sample memory's base: no register is free to hold it, so it is loaded per use.
		void loadEramMemory(size_t) {}
		Gp eramMemory()
		{
			a.ldr(x(11), mem(19, offsetof(JitFrame, eramMemory)));
			return x(11);
		}
		// Byte `high` of d := byte `high` of s; the rest of d stays.
		void insertByte(const Gp& d, const Gp& s, bool high, const Gp& scratch)
		{
			if (!high)
			{
				a.bfxil(d, s, 0, 8);
				return;
			}
			a.lsr(scratch, s, 8);
			a.bfi(d, scratch, 8, 8);
		}

		// Clamp to signed 24 bits: bias to [0, 0xffffff], and only overflow takes the bound from
		// the sign. Most samples need no clamping, so the clamp is a cold stub (emitColdCode).
		// B.cond reaches +/-1 MiB and a large graph's kernel can exceed that: once the oldest
		// pending stub would get out of reach, the pending stubs are parked right here behind a
		// jump instead of past the return.
		struct Clamp
		{
			asmjit::Label stub, back;
			unsigned reg;
			size_t origin;
		};
		static constexpr size_t kClampStubBytes = 12, kClampReach = 768 * 1024;
		std::vector<Clamp> clamps;
		// The pending stubs, here, behind a jump over them.
		void parkColdCode()
		{
			if (clamps.empty())
				return;
			auto over = a.newLabel();
			a.b(over);
			emitColdCode();
			a.bind(over);
		}
		// Checked at every block start and clamp; a block or an operation never adds the
		// remaining quarter of the reach.
		void parkColdCodeIfFar()
		{
			if (!clamps.empty() && a.offset() - clamps.front().origin + clamps.size() * kClampStubBytes > kClampReach)
				parkColdCode();
		}
		void saturate(unsigned reg)
		{
			parkColdCodeIfFar();
			Clamp clamp{a.newLabel(), a.newLabel(), reg, a.offset()};
			a.add(w(10), w(reg), 0x800000);
			a.tst(w(10), 0xff000000);
			a.b(CondCode::kNE, clamp.stub);
			a.bind(clamp.back);
			clamps.push_back(clamp);
		}
		void emitColdCode()
		{
			for (const auto& clamp : clamps)
			{
				a.bind(clamp.stub);
				a.asr(w(clamp.reg), w(clamp.reg), 31);
				a.eor(w(clamp.reg), w(clamp.reg), 0x7fffff);
				a.b(clamp.back);
			}
			clamps.clear();
		}
		void finishFunction() {}
		// w6 signed sample -> w7 packed code. The exponent is the magnitude's bit count above
		// 17 in pairs: 16 - clz, clamped at zero, with the low bit dropped.
		void packEramNative()
		{
			using namespace asmjit::a64;
			a.sbfx(w(6), w(6), 0, 24);
			a.eor(w(8), w(6), w(6), asr(31));
			a.clz(w(8), w(8));
			a.subs(w(9), w(8), 16);
			a.csneg(w(9), wzr, w(9), CondCode::kGE);
			a.and_(w(9), w(9), 6);
			a.add(w(8), w(9), 4);
			a.asr(w(7), w(6), w(8));
			a.and_(w(7), w(7), 0x3fff);
			a.orr(w(7), w(7), w(9), lsl(13));
		}

		void enterCore(unsigned, size_t coreOffset) { a.add(x(24), x(20), coreOffset); }
		void beginBlock() { parkColdCodeIfFar(); }
		void beginOperation() {}
		void setLastInput() { a.mov(w(29), w(0)); }
		// The retained multiplicand is about to be written in place (load, constant or shift).
		void beginLastInput() {}
		void shiftLastInput(unsigned amount) { a.asr(w(29), w(29), amount); }

		// The operand is in w(operandReg), a 24-bit value when operand24; the MUL30/DMAC
		// coefficient in w1 is a signed 32-bit value already.
		void emitProduct(const CoreProgram::Operation& op, const Instr& in, unsigned pc, bool specialCoef,
			unsigned operandReg, bool operand24)
		{
			using namespace asmjit::a64;
			const unsigned dst = op.destination, current = op.current[in.accB];
			if (op.constantShift >= 0)
			{
				// The built-in multiplicand is a power of two: fold its shift into the add.
				const int shift = op.constantShift - 7;
				a.ldrsh(w(in.replace ? dst : 1), mem(23, pc * 2));
				if (in.replace)
				{
					if (shift < 0)
						a.asr(w(dst), w(dst), -shift);
					else
						a.lsl(w(dst), w(dst), shift);
				}
				else if (shift < 0)
					a.add(w(dst), w(current), w(1), asr(-shift));
				else
					a.add(w(dst), w(current), w(1), lsl(shift));
				return;
			}
			unsigned operand = operandReg;
			if (!operand24)
			{
				a.sbfx(w(0), w(operandReg), 0, 24);
				operand = 0;
			}
			if (specialCoef)
			{
				a.smull(x(2), w(operand), w(1));
				a.ldrb(w(3), mem(23, offsetof(LiveTables, shift) + pc));
				a.asr(x(in.replace ? dst : 2), x(2), x(3));
				if (!in.replace)
					a.add(w(dst), w(2), w(current));
				return;
			}
			a.ldrsh(w(1), mem(23, pc * 2));
			a.smull(x(2), w(operand), w(1));
			if (in.replace)
				a.asr(x(dst), x(2), 7);
			else
				a.add(x(dst), x(current), x(2), asr(7));
		}

		// Move the block's live pipeline values to the fixed edge registers. A value a product
		// wrote in this block is wrapped to 30 bits; the others arrived wrapped.
		void normalize(const CoreProgram::Block& block, unsigned written)
		{
			for (unsigned b = 0; b < 2; ++b)
			{
				const unsigned base = b ? 15 : 12, current = b ? 27 : 26;
				const unsigned live = (block.liveOut >> (b * 4)) & 15;
				const auto source = [&](unsigned k) { return k == 3 ? block.current[b] : block.history[b][k]; };
				const auto edge = [&](unsigned k) { return k == 3 ? current : base + k; };
				bool wrapped[32] = {};
				for (unsigned k = 0; k < 4; ++k)
					if ((live & (1u << k)) && (written & (1u << source(k))) && !wrapped[source(k)])
					{
						a.sbfx(w(source(k)), w(source(k)), 0, 30);
						wrapped[source(k)] = true;
					}
				// Through temporaries: an edge can permute live registers.
				for (unsigned k = 0; k < 4; ++k)
					if ((live & (1u << k)) && source(k) != edge(k))
						a.mov(w(k), w(source(k)));
				for (unsigned k = 0; k < 4; ++k)
					if ((live & (1u << k)) && source(k) != edge(k))
						a.mov(w(edge(k)), w(k));
			}
		}

		void prologue(unsigned predicateStack)
		{
			for (unsigned r = 19; r <= 29; r += 2)
				a.stp(x(r), x(r + 1), Mem(asmjit::a64::sp, -16).pre());
			if (predicateStack)
				a.sub(asmjit::a64::sp, asmjit::a64::sp, predicateStack);
			a.mov(x(19), x(0));
		}
		void setEramBase(size_t offset)
		{
			a.mov(x(25), offset);
			a.add(x(25), x(20), x(25));
		}
		void clearRequests(size_t offset)
		{
			a.add(x(10), x(19), offset);
			for (unsigned i = 0; i < FRAME_CLOCKS / 16; ++i)
				a.stp(asmjit::a64::xzr, asmjit::a64::xzr, asmjit::a64::ptr(x(10), 16).post());
		}
		void epilogue(unsigned predicateStack)
		{
			if (predicateStack)
				a.add(asmjit::a64::sp, asmjit::a64::sp, predicateStack);
			for (int r = 29; r >= 19; r -= 2)
				a.ldp(x(r), x(r + 1), Mem(asmjit::a64::sp, 16).post());
			a.ret(x(30));
		}

		// Rejects code with calls, indirect branches or jumps outside the function.
		static KernelFn finish(asmjit::JitRuntime& runtime, asmjit::CodeHolder& code, Assembler&, bool failed)
		{
			if (failed)
				return nullptr;
			KernelFn fn = nullptr;
			if (runtime.add(&fn, &code) != asmjit::kErrorOk)
				return nullptr;
			const auto* instructions = reinterpret_cast<const uint32_t*>(fn);
			const size_t count = code.codeSize() / sizeof(uint32_t);
			for (size_t i = 0; i < count; ++i)
			{
				const uint32_t word = instructions[i];
				const bool indirect = (word & 0xfffffc1fu) == 0xd63f0000u || (word & 0xfffffc1fu) == 0xd61f0000u;
				const int64_t target = int64_t(i) + se<26>(int32_t(word & 0x03ffffffu));
				const bool directOutside = (word & 0x7c000000u) == 0x14000000u && (target < 0 || target >= int64_t(count));
				if (indirect || directOutside || (word & 0xfc000000u) == 0x94000000u)
				{
					runtime.release(fn);
					return nullptr;
				}
			}
			return fn;
		}
	};

	template <int N> using JitArm64 = JitEmitter<Arm64Isa<N>>;
}
#endif
