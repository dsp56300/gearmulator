#include "esp_jit_arm64.h"

#include <array>

#include "esp.hpp"

#include "asmjit/arm/a64operand.h"

// Register Usage:
// inputs:
// x0  ptr to coefs
// x1  ptr to iram
// x2  ptr to gram
// x3  ptr to variables
// x4  eramPos
// x5  iramPos
// x6  satMin (-0x800000), set at entry
// x7  satMax ( 0x7fffff), set at entry
// x0/x1/x2 are rewritten at entry: x0 = eram ptr, x1/x2 = iram/gram + iramPos*4

// state:
// x8  eramEffectiveAddr
// x9  eramWriteLatchNext
// x10 eramReadLatch
// x11 eramWriteLatch
// x12 eramVarOffset
// x13 condition
// x14 last_mulInputA_24
// x15 last_mulInputB_24

// x19 acc0
// x20 acc1
// x21 acc2
// x22 acc3
// x23 acc4
// x24 acc5
// x25 (temp)  readAcc
// x26 (temp)  mempos
// x27 (temp)  mulInA
// x28 (temp)  mulInB
	
namespace esp
{
	using namespace asmjit::a64;
	using namespace asmjit::a64::regs;

	constexpr auto ptrEram = x0;   // coefs are read via ptrVars; x0 is repurposed at entry

	constexpr auto ptrIram = x1;
	constexpr auto ptrGram = x2;
	constexpr auto ptrVars = x3;
	constexpr auto eramPos = x4;
	constexpr auto iramPos = x5;

	constexpr auto eramEffectiveAddr = x8;
	constexpr auto eramWriteLatchNext = x9;
	constexpr auto eramReadLatch = x10;
	constexpr auto eramWriteLatch = x11;
	constexpr auto eramVarOffset = x12;

	constexpr auto satMin = x6;    // -0x800000
	constexpr auto satMax = x7;    //  0x7fffff
	constexpr auto condition = x13;

	constexpr auto last_mulInputA_24 = x14;
	constexpr auto last_mulInputB_24 = x15;

	constexpr auto acc0 = x19;
	constexpr auto acc1 = x20;
	constexpr auto acc2 = x21;
	constexpr auto acc3 = x22;
	constexpr auto acc4 = x23;
	constexpr auto acc5 = x24;

	constexpr std::array<GpX, 6> acc{acc0, acc1, acc2, acc3, acc4, acc5};

	constexpr auto tempA = x25;
	constexpr auto tempB = x26;
	constexpr auto mulInA = x27;
	constexpr auto mulInB = x28;
	
	EspJitArm64::EspJitArm64(Asm& a, const JitInputData& _data) : m_asm(a), m_data(_data)
	{
	}

	void EspJitArm64::jitEnter()
	{
	    // save x19-x28
	    m_asm.stp(x29, x30, Mem(sp, -16).pre());   // [sp, #-16]!
	    m_asm.mov(x29, sp);
	    m_asm.stp(x19, x20, Mem(sp, -16).pre());
	    m_asm.stp(x21, x22, Mem(sp, -16).pre());
	    m_asm.stp(x23, x24, Mem(sp, -16).pre());
	    m_asm.stp(x25, x26, Mem(sp, -16).pre());
	    m_asm.stp(x27, x28, Mem(sp, -16).pre());

	    /* The ESP's ERAM latch chain, DMAC inputs and accumulators are state that
	     * PERSISTS across samples: the interpreter keeps them in the ESP object and
	     * the x64 backend loads and stores them through JitInputData. Load the real
	     * state here and write it back in jitExit -- entry register content must
	     * never depend on what the caller left behind. */
	    m_asm.mov(tempA, (uint64_t)(uintptr_t)m_data.eramEffectiveAddr);
	    m_asm.ldr(eramEffectiveAddr.w(), ptr(tempA));
	    m_asm.mov(tempA, (uint64_t)(uintptr_t)m_data.eramWriteLatchNext);
	    m_asm.ldrsw(eramWriteLatchNext, ptr(tempA));
	    m_asm.mov(tempA, (uint64_t)(uintptr_t)m_data.eramReadLatch);
	    m_asm.ldrsw(eramReadLatch, ptr(tempA));
	    m_asm.mov(tempA, (uint64_t)(uintptr_t)m_data.eramWriteLatch);
	    m_asm.ldrsw(eramWriteLatch, ptr(tempA));
	    m_asm.mov(tempA, (uint64_t)(uintptr_t)m_data.eramVarOffset);
	    m_asm.ldrsw(eramVarOffset, ptr(tempA));
	    m_asm.mov(tempA, (uint64_t)(uintptr_t)m_data.last_mulInputA_24);
	    m_asm.ldrsw(last_mulInputA_24, ptr(tempA));
	    m_asm.mov(tempA, (uint64_t)(uintptr_t)m_data.last_mulInputB_24);
	    m_asm.ldrsw(last_mulInputB_24, ptr(tempA));
	    for (int i = 0; i < 6; ++i)
	        m_asm.ldr(acc[i], ptr(ptrVars, int32_t(offsetof(CoreData, accs) + i * sizeof(int64_t))));
	    // No pointer exists for these; give them a deterministic entry value.
	    m_asm.mov(condition, 0);
	    m_asm.mov(tempB, 0);
	    m_asm.mov(mulInA, 0);
	    m_asm.mov(mulInB, 0);

	    // Rebase the rings once: every iram/gram access is then [base, #mem*4] (ESP_IRAM_MIRROR).
	    m_asm.add(ptrIram, ptrIram, iramPos, lsl(2));
	    m_asm.add(ptrGram, ptrGram, iramPos, lsl(2));
	    // Hoisted constants and pointers
	    m_asm.ldr(ptrEram, ptr(ptrVars, offsetof(CoreData, eramPtr)));
	    m_asm.mov(satMin, -0x800000);
	    m_asm.mov(satMax, 0x7fffff);
	    m_asm.mov(tempA, 0);
	}

	void EspJitArm64::jitExit()
	{
	    // Write the persistent state back; see jitEnter.
	    m_asm.mov(tempA, (uint64_t)(uintptr_t)m_data.eramEffectiveAddr);
	    m_asm.str(eramEffectiveAddr.w(), ptr(tempA));
	    m_asm.mov(tempA, (uint64_t)(uintptr_t)m_data.eramWriteLatchNext);
	    m_asm.str(eramWriteLatchNext.w(), ptr(tempA));
	    m_asm.mov(tempA, (uint64_t)(uintptr_t)m_data.eramReadLatch);
	    m_asm.str(eramReadLatch.w(), ptr(tempA));
	    m_asm.mov(tempA, (uint64_t)(uintptr_t)m_data.eramWriteLatch);
	    m_asm.str(eramWriteLatch.w(), ptr(tempA));
	    m_asm.mov(tempA, (uint64_t)(uintptr_t)m_data.eramVarOffset);
	    m_asm.str(eramVarOffset.w(), ptr(tempA));
	    m_asm.mov(tempA, (uint64_t)(uintptr_t)m_data.last_mulInputA_24);
	    m_asm.str(last_mulInputA_24.w(), ptr(tempA));
	    m_asm.mov(tempA, (uint64_t)(uintptr_t)m_data.last_mulInputB_24);
	    m_asm.str(last_mulInputB_24.w(), ptr(tempA));
	    for (int i = 0; i < 6; ++i)
	        m_asm.str(acc[i], ptr(ptrVars, int32_t(offsetof(CoreData, accs) + i * sizeof(int64_t))));

	    // restore x19-x28
	    m_asm.ldp(x27, x28, Mem(sp, 16).post(0));   // [sp], #16
	    m_asm.ldp(x25, x26, Mem(sp, 16).post(0));
	    m_asm.ldp(x23, x24, Mem(sp, 16).post(0));
	    m_asm.ldp(x21, x22, Mem(sp, 16).post(0));
	    m_asm.ldp(x19, x20, Mem(sp, 16).post(0));
	    m_asm.ldp(x29, x30, Mem(sp, 16).post(0));
	    m_asm.ret(x30);
	}

	void EspJitArm64::eramRead(uint32_t eramMask)
	{
		// eramReadLatch = se<24>(eram[eramEffectiveAddr & ERAM_MASK]);
		m_asm.and_(eramEffectiveAddr, eramEffectiveAddr, eramMask); // eramEffectiveAddr = eramEffectiveAddr & ERAM_MASK
		m_asm.ldrsw(eramReadLatch, ptr(ptrEram, eramEffectiveAddr, lsl(2))); // load eram[eramEffectiveAddr & ERAM_MASK]
	}

	void EspJitArm64::eramWrite(uint32_t eramMask)
	{
		// TODO:
		// The ESP compresses ERAM data with some loss of precision, we don't emulate that on the JIT for performance
		
		// inline static int crunch(int x) {
		//   const int b = ((x >> 1) & 0x400000) * 3;
		//   if (((x << 1) & 0xc00000) != b) return x & 0xFFFFFC00;
		//   if (((x << 3) & 0xc00000) != b) return x & 0xFFFFFF00;
		//   if (((x << 5) & 0xc00000) != b) return x & 0xFFFFFFC0;
		//   return x & 0xFFFFFFF0;
		// }

		// eram[eramEffectiveAddr & ERAM_MASK] = crunch(eramWriteLatchNext);
		m_asm.and_(eramEffectiveAddr, eramEffectiveAddr, eramMask); // eramEffectiveAddr = eramEffectiveAddr & ERAM_MASK
		m_asm.str(w9, ptr(ptrEram, eramEffectiveAddr, lsl(2))); // store eramWriteLatchNext
	}

	void EspJitArm64::eramComputeAddr(uint32_t immOffset, bool highOffset, bool shouldUseVarOffset)
	{
      // eramWriteLatchNext = eramWriteLatch;
      m_asm.mov(eramWriteLatchNext, eramWriteLatch);

      // eramEffectiveAddr = eramPos + immOffset;
      m_asm.mov(eramEffectiveAddr, immOffset);
      m_asm.add(eramEffectiveAddr, eramEffectiveAddr, eramPos);
      
      if (shouldUseVarOffset)
      {
        // eramEffectiveAddr += eramVarOffset >> 12;
        m_asm.add(eramEffectiveAddr, eramEffectiveAddr, eramVarOffset, lsr(12));
      }
      if (highOffset)
      {
        // eramEffectiveAddr += (eramPos <= 0x4000) ? 0x40000 : 0xc0000;
        m_asm.cmp(eramPos, 0x4000);
        m_asm.mov(tempA, 0x40000);
        m_asm.mov(tempB, 0xc0000);
        m_asm.csel(tempB, tempB, tempA, CondCode::kGT);
        m_asm.add(eramEffectiveAddr, eramEffectiveAddr, tempB);
      }
	}

	void EspJitArm64::emitOp(uint32_t pc, const ESPOptInstr& instr, const bool lastMul30, const bool nextIsDmac)
	{
		const bool doesMac = !instr.m_access.nomac && instr.m_access.srcReg != -1 && instr.m_access.destReg != -1;
		// Is mulInputA_24 consumed at all? Only by this op's MAC (a following DMAC reads
		// last_mulInputA_24, which is mulInputA only when this op MACs and 0 otherwise).
		const bool needA = doesMac;
		const uint32_t memOff = (uint32_t)instr.mem << 2;

		// Register holding sat(readAcc) (or the raw acc for the unsat ops)
		GpX satReg = tempA;
		if (instr.m_access.save)
		{
			const auto src = acc[instr.m_access.readReg];
			const bool unsat = instr.opType == kStoreIRAMUnsat || instr.opType == kWriteEramVarOffset;
			if (unsat)
			{
				satReg = src;
			}
			else
			{
				m_asm.cmp(src, satMin);
				m_asm.csel(tempA, src, satMin, CondCode::kGE);
				m_asm.cmp(tempA, satMax);
				m_asm.csel(tempA, tempA, satMax, CondCode::kLE);
			}
		}

		// Ops whose mulInputA comes from iram[mem] (or the immediate)
		bool srcIsIram;
		switch (instr.opType)
		{
		case kDMAC: case kStoreIRAM: case kReadGRAM: case kStoreGRAM: case kStoreIRAMUnsat: case kStoreIRAMRect:
		case kReadEramReadLatch: case kInterpStorePos: case kInterpStoreNeg:
			srcIsIram = false; break;
		case kMulCoef:
			srcIsIram = !(instr.coef & 4); break;
		default:
			srcIsIram = true; break;
		}

		// Register holding the raw mulInputA_24 once the op-specific part is done
		GpX aReg = mulInA;

		if (srcIsIram && needA)
		{
			if (instr.useImm)
				m_asm.mov(mulInA, instr.imm);
			else
				m_asm.ldrsw(mulInA, ptr(ptrIram, memOff));
		}

		// A plain MAC takes B from the coef byte and shifts by shiftAmounts[pc]. Both are live (the H8S
		// rewrites them without a recompile), so they must stay memory loads - but (A*B) >> s ==
		// (A*(B << (7-s))) >> 7 exactly, so one pre-shifted int16 load and an immediate shift do it.
		const bool bFromCoef = doesMac && instr.opType != kMulCoef && !(instr.opType == kDMAC && lastMul30);
		if (bFromCoef)
			m_asm.ldrsh(mulInB, ptr(ptrVars, offsetof(CoreData, coefsShifted) + pc * 2));

		switch (instr.opType)
		{
		case kDMAC:
			// mulInputA_24 = last_mulInputA_24 >> 7;
			if (needA) m_asm.asr(mulInA, last_mulInputA_24, 7);
			if (lastMul30 && doesMac)
			{
				// mulInputB_24 = (last_mulInputB_24 >> 9) & 0x7f;
				m_asm.asr(mulInB, last_mulInputB_24, 9);
				m_asm.and_(mulInB, mulInB, 0x7f);
			}
			break;
		case kInterp:
			// mulInputA_24 = (~mulInputA_24 & 0x7fffff);
			if (needA)
			{
				m_asm.mvn(mulInA, mulInA);
				m_asm.and_(mulInA, mulInA, 0x7fffff);
			}
			break;

		case kStoreIRAM:
		case kStoreIRAMUnsat:
			// iram[mempos] = mulInputA_24 = sat(readAcc);  (Unsat: readAcc)
			m_asm.str(satReg.w(), ptr(ptrIram, memOff));
			aReg = satReg;
			break;
		case kReadGRAM:
			// mulInputA_24 = gram[mempos];
			if (needA) m_asm.ldrsw(mulInA, ptr(ptrGram, memOff));
			break;
		case kStoreGRAM:
			// gram[mempos] = mulInputA_24 = sat(readAcc);
			m_asm.str(satReg.w(), ptr(ptrGram, memOff));
			aReg = satReg;
			break;
		case kStoreIRAMRect:
			// iram[mempos] = mulInputA_24 = std::max(0, sat(readAcc));
			m_asm.cmp(satReg, 0);
			m_asm.csel(mulInA, satReg, xzr, CondCode::kGE);
			m_asm.str(mulInA.w(), ptr(ptrIram, memOff));
			break;

		case kWriteEramVarOffset:
			// eram.eramVarOffset = readAcc;
			m_asm.mov(eramVarOffset, satReg);
			break;
		case kWriteHost:
			// *((int32_t*)&readback_regs) = sat(readAcc);
			m_asm.ldr(tempB, ptr(ptrVars, offsetof(CoreData, hostRegPtr)));
			m_asm.str(satReg.w(), ptr(tempB));
			break;
		case kWriteEramWriteLatch:
			// eram.eramWriteLatch = sat(readAcc);
			m_asm.mov(eramWriteLatch, satReg);
			break;
		case kReadEramReadLatch:
			// mulInputA_24 = eram.eramReadLatch;
			// writeIRAM(mulInputA_24, instr.mem | 0xf0);
			m_asm.str(eramReadLatch.w(), ptr(ptrIram, (uint32_t)(instr.mem | 0xf0) << 2));
			aReg = eramReadLatch;
			break;
		case kWriteMulCoef:
			// mulcoeffs[(instr.mem >> 1) & 7] = sat(readAcc);
			m_asm.str(satReg.w(), ptr(ptrVars, offsetof(CoreData, mulcoeffs) + (((instr.mem >> 1) & 7) << 2)));
			break;

		case kMulCoef:
			{
				const bool weird = (instr.coef & 0x1c) == 0x1c;

				if (instr.coef & 4)
				{
					// mulInputA_24 = sat(readAcc);
					if (weird)
					{
						// mulInputA_24 = (mulInputA_24 >= 0) ? 0x7fffff : 0xFF800000;
						m_asm.cmp(satReg, 0);
						m_asm.mov(mulInA, 0xFF800000);
						m_asm.csel(mulInA, satMax, mulInA, CondCode::kGE);
					}
					else
					{
						aReg = satReg;
					}
					// iram[mempos] = mulInputA_24;
					m_asm.str(aReg.w(), ptr(ptrIram, memOff));
				}

				// mulInputB_24 is consumed by this MAC and (pre-shift) by an immediately following DMAC
				if (doesMac || nextIsDmac)
				{
					if ((instr.coef >> 5) == 6)
					{
						// mulInputB_24 = (shared.eram.eramVarOffset << 11) & 0x7fffff;
						m_asm.lsl(mulInB, eramVarOffset, 11);
						m_asm.and_(mulInB, mulInB, 0x7fffff);
					}
					else if ((instr.coef >> 5) == 7)
					{
						// mulInputB_24 = shared.mulcoeffs[5];
						m_asm.ldrsw(mulInB, ptr(ptrVars, offsetof(CoreData, mulcoeffs) + (5 << 2)));
					}
					else
					{
						// mulInputB_24 = shared.mulcoeffs[coef >> 5];
						m_asm.ldrsw(mulInB, ptr(ptrVars, offsetof(CoreData, mulcoeffs) + (((instr.coef >> 5) & 7) << 2)));
					}

					if ((instr.coef & 8) && !weird)
					{
						// mulInputB_24 *= -1;
						m_asm.neg(mulInB, mulInB);
					}

					if ((instr.coef & 16) && !weird)
					{
						// if (mulInputB_24 >= 0) mulInputB_24 = (~mulInputB_24 & 0x7fffff);
						// else mulInputB_24 = ~(mulInputB_24 & 0x7fffff);
						m_asm.mvn(tempB, mulInB);
						m_asm.cmp(mulInB, 0);
						m_asm.and_(tempB, tempB, 0x7fffff);
						m_asm.and_(mulInB, mulInB, 0x7fffff);
						m_asm.csinv(mulInB, tempB, mulInB, CondCode::kGE);
					}

					// last_mulInputB_24 = mulInputB_24;
					if (nextIsDmac) m_asm.mov(last_mulInputB_24, mulInB);

					// mulInputB_24 >>= 16;
					if (doesMac) m_asm.asr(mulInB, mulInB, 16);
				}
			}
			break;

		case kInterpStorePos:
		case kInterpStoreNeg:
			// iram[mempos] = mulInputA_24 = sat(readAcc);
			m_asm.str(satReg.w(), ptr(ptrIram, memOff));
			if (needA)
			{
				// Pos: if (mulInputA_24 >= 0) mulInputA_24 = ~mulInputA_24;   Neg: if (< 0)
				m_asm.mvn(mulInA, satReg);
				m_asm.cmp(satReg, 0);
				m_asm.csel(mulInA, mulInA, satReg, instr.opType == kInterpStorePos ? CondCode::kGE : CondCode::kLT);
				// mulInputA_24 &= 0x7fffff;
				m_asm.and_(mulInA, mulInA, 0x7fffff);
			}
			break;

		default:
			break;
		}

		if (doesMac)
		{
			// last_mulInputA_24 = mulInputA_24;
			if (nextIsDmac) m_asm.mov(last_mulInputA_24, aReg);

			const int64_t srcAcc = instr.m_access.srcReg;
			const int64_t destAcc = instr.m_access.destReg;

			// result = (int64_t)se<24>(mulInputA_24) * (int64_t) mulInputB_24;
			m_asm.sbfx(mulInA, aReg, 0, 24);
			m_asm.mul(tempA, mulInA, mulInB);

			// result >>= instr.shiftAmount;  (live, see coefs)
			if (bFromCoef)
			{
				// B was pre-shifted by (7 - shiftAmount)
				if (instr.m_access.clr)
					m_asm.asr(acc[destAcc], tempA, 7);
				else
				{
					m_asm.asr(tempA, tempA, 7);
					m_asm.add(acc[destAcc], acc[srcAcc], tempA);
				}
			}
			else
			{
				m_asm.ldrsb(tempB, ptr(ptrVars, offsetof(CoreData, shiftAmounts) + pc));
				if (instr.m_access.clr)
				{
					// *destAcc = result;
					m_asm.asr(acc[destAcc], tempA, tempB);
				}
				else
				{
					// *destAcc = result + *srcAcc;
					m_asm.asr(tempA, tempA, tempB);
					m_asm.add(acc[destAcc], acc[srcAcc], tempA);
				}
			}
		}
		else if (nextIsDmac)
		{
			// last_mulInputA_24 = 0;
			m_asm.mov(last_mulInputA_24, 0);
		}
	}
}
