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
// x6  (reserved)
// x7  (reserved)

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

	constexpr auto ptrCoefs = x0;

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
	
	EspJitArm64::EspJitArm64(Asm& a, const JitInputData&) : m_asm(a)
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
	}

	void EspJitArm64::jitExit()
	{
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
		m_asm.ldr(tempA, ptr(ptrVars, offsetof(CoreData, eramPtr))); // load eram ptr
		m_asm.ldrsw(eramReadLatch, ptr(tempA, eramEffectiveAddr, lsl(2))); // load eram[eramEffectiveAddr & ERAM_MASK]
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
		m_asm.ldr(tempA, ptr(ptrVars, offsetof(CoreData, eramPtr))); // load eram ptr
		m_asm.str(w9, ptr(tempA, eramEffectiveAddr, lsl(2))); // store eramWriteLatchNext
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

	void EspJitArm64::emitOp(uint32_t pc, const ESPOptInstr& instr, const bool lastMul30)
	{
		if (instr.m_access.save)
		{
			// readAcc = acc[instr.m_access.readReg];
			m_asm.mov(tempA, acc[instr.m_access.readReg]);

			// pre-saturate
			bool unsat = instr.opType == kStoreIRAMUnsat || instr.opType == kWriteEramVarOffset;
			if (!unsat)
			{
				m_asm.mov(tempB, -0x800000);
				m_asm.cmp(tempA, tempB);
				m_asm.csel(tempA, tempA, tempB, CondCode::kGE);
				m_asm.mov(tempB, 0x7fffff);
				m_asm.cmp(tempA, tempB);
				m_asm.csel(tempA, tempA, tempB, CondCode::kLE);
			}
		}

		if (instr.opType != kDMAC)
		{
			// const uint32_t mempos = ((uint32_t)instr.mem + iramPos) & IRAM_MASK;
			m_asm.add(tempB, iramPos, instr.mem); // tempB = instr.mem + iramPos
			m_asm.and_(tempB, tempB, 0xff); // tempB &= 0xff

			// int32_t mulInputA_24 = 0;
			if (instr.useImm)
			{
				// mulInputA_24 = instr.imm;
				m_asm.mov(mulInA, instr.imm);
			}
			else
			{
				// mulInputA_24 = iram[mempos];
				m_asm.ldrsw(mulInA, ptr(ptrIram, tempB, lsl(2)));
			}
		}

		if (instr.opType != kMulCoef && !(instr.opType == kDMAC && lastMul30))
		{
			// int32_t mulInputB_24 = instr.coef;
			// m_asm.mov(tempD, (int64_t)instr.coefSigned); // TODO: separate coefs

			m_asm.ldrsb(mulInB, ptr(ptrVars, offsetof(CoreData, coefs) + pc));
		}

		bool setcondition = false;
		switch (instr.opType)
		{
		case kDMAC:
			// mulInputA_24 = last_mulInputA_24 >> 7;
			m_asm.asr(mulInA, last_mulInputA_24, 7);
			if (lastMul30)
			{
				// mulInputB_24 = (last_mulInputB_24 >> 9) & 0x7f;
				m_asm.asr(mulInB, last_mulInputB_24, 9);
				m_asm.and_(mulInB, mulInB, 0x7f);
			}
			break;
		case kInterp:
			// mulInputA_24 = (~mulInputA_24 & 0x7fffff);
			m_asm.mvn(mulInA, mulInA);
			m_asm.and_(mulInA, mulInA, 0x7fffff);
			break;

		case kStoreIRAM:
			// iram[mempos] = mulInputA_24 = sat(readAcc);
			m_asm.mov(mulInA, tempA);
			m_asm.str(mulInA.w(), ptr(ptrIram, tempB, lsl(2)));
			break;
		case kReadGRAM:
			// mulInputA_24 = gram[mempos];
			m_asm.ldrsw(mulInA, ptr(ptrGram, tempB, lsl(2)));
			break;
		case kStoreGRAM:
			// gram[mempos] = mulInputA_24 = sat(readAcc);
			m_asm.mov(mulInA, tempA);
			m_asm.str(mulInA.w(), ptr(ptrGram, tempB, lsl(2)));
			break;
		case kStoreIRAMUnsat:
			// iram[mempos] = mulInputA_24 = readAcc;
			m_asm.mov(mulInA, tempA);
			m_asm.str(mulInA.w(), ptr(ptrIram, tempB, lsl(2)));
			break;
		case kStoreIRAMRect:
			// iram[mempos] = mulInputA_24 = std::max(0, sat(readAcc));
			m_asm.mov(mulInA, tempA);
			m_asm.mov(tempA, 0);
			m_asm.cmp(mulInA, tempA);
			m_asm.csel(mulInA, mulInA, tempA, CondCode::kGE);
			m_asm.str(mulInA.w(), ptr(ptrIram, tempB, lsl(2)));
			break;

		case kWriteEramVarOffset:
			// eram.eramVarOffset = readAcc;
			m_asm.mov(eramVarOffset, tempA);
			break;
		case kWriteHost:
			// *((int32_t*)&readback_regs) = sat(readAcc);
			m_asm.ldr(tempB, ptr(ptrVars, offsetof(CoreData, hostRegPtr)));
			m_asm.str(tempA.w(), ptr(tempB));
			break;
		case kWriteEramWriteLatch:
			// eram.eramWriteLatch = sat(readAcc);
			m_asm.mov(eramWriteLatch, tempA);
			break;
		case kReadEramReadLatch:
			// mulInputA_24 = eram.eramReadLatch;
			m_asm.mov(mulInA, eramReadLatch);

			// writeIRAM(mulInputA_24, instr.mem | 0xf0);
			m_asm.add(tempB, iramPos, instr.mem | 0xf0); // tempB = instr.mem + iramPos
			m_asm.and_(tempB, tempB, 0xff); // tempB &= 0xff
			m_asm.str(w10, ptr(ptrIram, tempB, lsl(2)));
			break;
		case kWriteMulCoef:
			// mulcoeffs[(instr.mem >> 1) & 7] = sat(readAcc);
			m_asm.add(tempB, ptrVars, offsetof(CoreData, mulcoeffs));
			m_asm.str(tempA.w(), ptr(tempB, ((instr.mem >> 1) & 7) << 2));
			break;

		case kMulCoef:
			{
				bool weird = (instr.coef & 0x1c) == 0x1c;

				if (instr.coef & 4)
				{
					// mulInputA_24 = sat(readAcc);
					m_asm.mov(mulInA, tempA);
					if (weird)
					{
						// mulInputA_24 = (mulInputA_24 >= 0) ? 0x7fffff : 0xFF800000;
						m_asm.mov(tempA, 0);
						m_asm.cmp(mulInA, tempA);
						m_asm.mov(tempA, 0x7fffff);
						m_asm.mov(mulInA, 0xFF800000);
						m_asm.csel(mulInA, tempA, mulInA, CondCode::kGE);
					}
					// iram[mempos] = mulInputA_24;
					m_asm.str(mulInA.w(), ptr(ptrIram, tempB, lsl(2)));
				}

				if ((instr.coef >> 5) == 6)
				{
					// mulInputB_24 = (shared.eram.eramVarOffset << 11) & 0x7fffff;
					m_asm.mov(mulInB, eramVarOffset);
					m_asm.lsl(mulInB, mulInB, 11);
					m_asm.and_(mulInB, mulInB, 0x7fffff);
				}
				else if ((instr.coef >> 5) == 7)
				{
					// mulInputB_24 = shared.mulcoeffs[5];
					m_asm.add(tempB, ptrVars, offsetof(CoreData, mulcoeffs));
					m_asm.ldrsw(mulInB, ptr(tempB, 5 << 2));
				}
				else
				{
					// mulInputB_24 = shared.mulcoeffs[coef >> 5];
					m_asm.add(tempB, ptrVars, offsetof(CoreData, mulcoeffs));
					m_asm.ldrsw(mulInB, ptr(tempB, ((instr.coef >> 5) & 7) << 2));
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
					m_asm.mvn(tempA, mulInB);
					m_asm.cmp(mulInB, 0);
					m_asm.and_(tempA, tempA, 0x7fffff);
					m_asm.and_(mulInB, mulInB, 0x7fffff);
					m_asm.csinv(mulInB, tempA, mulInB, CondCode::kGE);
				}

				// last_mulInputB_24 = mulInputB_24;
				m_asm.mov(last_mulInputB_24, mulInB);

				// mulInputB_24 >>= 16;
				m_asm.asr(mulInB, mulInB, 16);
			}
			break;

		case kInterpStorePos:
			// iram[mempos] = mulInputA_24 = sat(readAcc);
			m_asm.mov(mulInA, tempA);
			m_asm.str(mulInA.w(), ptr(ptrIram, tempB, lsl(2)));

			// if (mulInputA_24 >= 0) mulInputA_24 = ~mulInputA_24;
			m_asm.mvn(mulInA, mulInA);
			m_asm.cmp(tempA, 0);
			m_asm.csel(mulInA, mulInA, tempA, CondCode::kGE);

			// mulInputA_24 &= 0x7fffff;
			m_asm.and_(mulInA, mulInA, 0x7fffff);
			break;
		case kInterpStoreNeg:
			// iram[mempos] = mulInputA_24 = sat(readAcc);
			m_asm.mov(mulInA, tempA);
			m_asm.str(mulInA.w(), ptr(ptrIram, tempB, lsl(2)));

			// if (mulInputA_24 < 0) mulInputA_24 = ~mulInputA_24;
			m_asm.mvn(mulInA, mulInA);
			m_asm.cmp(tempA, 0);
			m_asm.csel(mulInA, mulInA, tempA, CondCode::kLT);

			// mulInputA_24 &= 0x7fffff;
			m_asm.and_(mulInA, mulInA, 0x7fffff);
			break;

		default:
			break;
		}

		const bool clr = instr.m_access.clr;
		if (!instr.m_access.nomac && instr.m_access.srcReg != -1 && instr.m_access.destReg != -1)
		{
			// last_mulInputA_24 = mulInputA_24;
			m_asm.mov(last_mulInputA_24, mulInA);

			int64_t srcAcc = instr.m_access.srcReg;
			int64_t destAcc = instr.m_access.destReg;

			// result = (int64_t)se<24>(mulInputA_24) * (int64_t) mulInputB_24;
			m_asm.and_(mulInA, mulInA, 0xffffff);
			m_asm.lsl(mulInA, mulInA, 64 - 24);
			m_asm.asr(mulInA, mulInA, 64 - 24);
			m_asm.mul(tempA, mulInA, mulInB);

			// result >>= instr.shiftAmount;
			// m_asm.asr(tempA, tempA, instr.shiftAmount);
			m_asm.ldrsb(mulInB, ptr(ptrVars, offsetof(CoreData, shiftAmounts) + pc));
			m_asm.asr(tempA, tempA, mulInB);

			if (!clr)
			{
				// result += *srcAcc;
				m_asm.add(tempA, tempA, acc[srcAcc]);
			}

			// *destAcc = result;
			m_asm.mov(acc[destAcc], tempA);
		}
		else
		{
			// last_mulInputA_24 = 0;
			m_asm.mov(last_mulInputA_24, 0);
		}
	}
}
