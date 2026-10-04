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

		// Clamp to signed 24 bits: bias to [0, 0xffffff], and only overflow takes the bound from the sign.
		void saturate(unsigned reg)
		{
			auto done = a.newLabel();
			a.add(w(10), w(reg), 0x800000);
			a.tst(w(10), 0xff000000);
			a.b(CondCode::kEQ, done);
			a.asr(w(reg), w(reg), 31);
			a.eor(w(reg), w(reg), 0x7fffff);
			a.bind(done);
		}
		// w6 signed sample -> w7 packed code
		void packEramNative()
		{
			using namespace asmjit::a64;
			a.sbfx(w(6), w(6), 0, 24);
			a.eor(w(8), w(6), w(6), asr(31));
			a.clz(w(8), w(8));
			a.mov(w(9), 16);
			a.subs(w(9), w(9), w(8));
			a.csel(w(9), w(9), wzr, CondCode::kGE);
			a.lsr(w(9), w(9), 1);
			a.lsl(w(8), w(9), 1);
			a.add(w(8), w(8), 4);
			a.asr(w(7), w(6), w(8));
			a.and_(w(7), w(7), 0x3fff);
			a.orr(w(7), w(7), w(9), lsl(14));
		}

		void enterCore(unsigned, size_t coreOffset) { a.add(x(24), x(20), coreOffset); }
		void beginBlock() {}
		void beginOperation() {}
		void setLastInput() { a.mov(w(29), w(0)); }

		// The operand is in w0, the MUL30/DMAC coefficient in w1.
		void emitProduct(const CoreProgram::Operation& op, const Instr& in, unsigned c, unsigned pc, bool specialCoef)
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
			if (!op.operand24)
				a.sbfx(w(0), w(0), 0, 24);
			if (specialCoef)
			{
				a.sbfx(w(1), w(1), 0, 8);
				a.smull(x(2), w(0), w(1));
				a.ldr(x(10), mem(19, offsetof(JitFrame, shift) + c * sizeof(void*)));
				a.ldrb(w(3), mem(10, pc));
				a.asr(x(in.replace ? dst : 2), x(2), x(3));
				if (!in.replace)
					a.add(w(dst), w(2), w(current));
				return;
			}
			a.ldrsh(w(1), mem(23, pc * 2));
			a.smull(x(2), w(0), w(1));
			if (in.replace)
				a.asr(x(dst), x(2), 7);
			else
				a.add(x(dst), x(current), x(2), asr(7));
		}

		// Move the block's live pipeline values to the fixed edge registers, wrapped to 30 bits.
		void normalize(const CoreProgram::Block& block)
		{
			for (unsigned b = 0; b < 2; ++b)
			{
				const unsigned base = b ? 15 : 12, current = b ? 27 : 26;
				const unsigned live = (block.liveOut >> (b * 4)) & 15;
				const auto source = [&](unsigned k) { return k == 3 ? block.current[b] : block.history[b][k]; };
				const auto edge = [&](unsigned k) { return k == 3 ? current : base + k; };
				bool wrapped[32] = {};
				for (unsigned k = 0; k < 4; ++k)
					if ((live & (1u << k)) && !wrapped[source(k)])
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
