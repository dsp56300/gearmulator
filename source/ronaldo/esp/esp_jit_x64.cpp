#include "esp_jit_x64.h"

#include <iostream>

#include "esp_jit_x64_types.h"
#include "esp.hpp"

#include "asmjit/x86/x86builder.h"

namespace esp
{
	using namespace asmjit::x86;

	EspJitX64::EspJitX64(Asm& a, const JitInputData& _data) : m_asm(a), m_data(_data), m_pool(a, g_regBasePtr, _data.coreData)
	{
	}

	void EspJitX64::jitEnter()
	{
		for (const auto& r : g_nonVolatileGPs)
		{
			if (r != rsp)
				m_asm.push(r);
		}

		m_asm.mov(g_regBasePtr, g_funcArgGPs[3]); // 4th arg is CoreData*
	}

	void EspJitX64::jitExit()
	{
		m_pool.clear();

		for (auto it = std::rbegin(g_nonVolatileGPs); it != std::rend(g_nonVolatileGPs); ++it)
		{
			const auto r = *it;
			if (r != rsp)
				m_asm.pop(r);
		}

		m_asm.ret();
	}

	void EspJitX64::eramRead(uint32_t _eramMask)
	{
		auto eramEffectiveAddr = m_pool.get(m_data.eramEffectiveAddr, Access::ReadWrite);

		// eramEffectiveAddr = eramEffectiveAddr & ERAM_MASK
		m_asm.and_(eramEffectiveAddr.r32(), _eramMask); 

		auto eramReadLatch = m_pool.get(m_data.eramReadLatch, Access::Write);

		// load eram[eramEffectiveAddr & ERAM_MASK]
		m_asm.movsxd(eramReadLatch, eramPtr(eramEffectiveAddr));
	}

	void EspJitX64::eramWrite(uint32_t _eramMask)
	{
		// TODO
		// inline static int crunch(int x) {
		//   const int b = ((x >> 1) & 0x400000) * 3;
		//   if (((x << 1) & 0xc00000) != b) return x & 0xFFFFFC00;
		//   if (((x << 3) & 0xc00000) != b) return x & 0xFFFFFF00;
		//   if (((x << 5) & 0xc00000) != b) return x & 0xFFFFFFC0;
		//   return x & 0xFFFFFFF0;
		// }

		// eram[eramEffectiveAddr & ERAM_MASK] = crunch(eramWriteLatchNext);

		// eramEffectiveAddr = eramEffectiveAddr & ERAM_MASK
		auto eramEffectiveAddr = m_pool.get(m_data.eramEffectiveAddr, Access::ReadWrite);
		m_asm.and_(eramEffectiveAddr, _eramMask);

		// load eram ptr
		auto eramWriteLatchNext = m_pool.get(m_data.eramWriteLatchNext, Access::Read);

		m_asm.mov(eramPtr(eramEffectiveAddr), eramWriteLatchNext.r32());
	}

	void EspJitX64::eramComputeAddr(uint32_t immOffset, bool highOffset, bool shouldUseVarOffset)
	{
		// eramWriteLatchNext = eramWriteLatch;
		{
			auto eramWriteLatch = m_pool.get(m_data.eramWriteLatch, Access::Read);
			auto eramWriteLatchNext = m_pool.get(m_data.eramWriteLatchNext, Access::Write);
			m_asm.mov(eramWriteLatchNext, eramWriteLatch);
		}

		// eramEffectiveAddr = eramPos + immOffset;
		auto eramEffectiveAddr = m_pool.get(m_data.eramEffectiveAddr, Access::Write);
		m_asm.mov(eramEffectiveAddr, immOffset);

		auto eramPos = m_pool.get(m_data.eramPos, Access::Read);
		m_asm.add(eramEffectiveAddr, eramPos);

		if (shouldUseVarOffset)
		{
			// eramEffectiveAddr += eramVarOffset >> 12;
			auto eramVarOffset = m_pool.get(m_data.eramVarOffset, Access::Read);
			auto tempA = m_pool.getTemp();
			m_asm.mov(tempA, eramVarOffset);
			m_asm.shr(tempA, 12);
			m_asm.add(eramEffectiveAddr, tempA);
		}
		if (highOffset)
		{
			// eramEffectiveAddr += (eramPos <= 0x4000) ? 0x40000 : 0xc0000;
			auto toAdd = m_pool.getTemp();
			m_asm.mov(toAdd, 0xc0000);

			auto temp = m_pool.getTemp();
			m_asm.mov(temp, 0x40000);

			m_asm.cmp(eramPos.r32(), 0x4000);
			m_asm.cmov(CondCode::kLE, toAdd, temp); // if (eramPos <= 0x4000) toAdd = 0x40000
			m_asm.add(eramEffectiveAddr, toAdd);
		}
	}

	void EspJitX64::checkUninit(const Gpq& reg) const
	{
		m_pool.checkUninit(reg);
	}

	void EspJitX64::emitOp(uint32_t pc, const ESPOptInstr& instr, bool lastMul30)
	{
		auto tempA = m_pool.getTemp();
		auto tempB = m_pool.getTemp();

		if (instr.m_access.save)
		{
			// readAcc = acc[instr.m_access.readReg];
			{
				auto acc = m_pool.get(&m_data.coreData->accs[instr.m_access.readReg], Access::Read);
				m_asm.mov(tempA, acc);
			}

			// pre-saturate
			bool unsat = instr.opType == kStoreIRAMUnsat || instr.opType == kWriteEramVarOffset;
			if (!unsat)
			{
				m_asm.mov(tempB, -0x800000);
				m_asm.cmp(tempA.r32(), tempB.r32());
				m_asm.cmov(CondCode::kL, tempA, tempB);
				m_asm.mov(tempB, 0x7fffff);
				m_asm.cmp(tempA.r32(), tempB.r32());
				m_asm.cmov(CondCode::kG, tempA, tempB);
			}
		}

		auto mulInA = m_pool.getTemp();

		if (instr.opType != kDMAC)
		{
			auto iramPos = m_pool.get(m_data.iramPos, Access::Read);
			// const uint32_t mempos = ((uint32_t)instr.mem + iramPos) & IRAM_MASK;
			m_asm.lea(tempB, ptr(iramPos, static_cast<int32_t>(instr.mem)));	// tempB = instr.mem + iramPos
			m_asm.and_(tempB, 0xff); // tempB &= 0xff

			// int32_t mulInputA_24 = 0;
			if (instr.useImm)
			{
				// mulInputA_24 = instr.imm;
				m_asm.mov(mulInA, instr.imm);
			}
			else
			{
				// mulInputA_24 = iram[mempos];
				m_asm.movsxd(mulInA, iramPtr(tempB));
			}
		}

		auto mulInB = m_pool.getTemp();	

		if (instr.opType != kMulCoef && !(instr.opType == kDMAC && lastMul30))
		{
			// int32_t mulInputB_24 = instr.coef;
			// m_asm.mov(tempD, (int64_t)instr.coefSigned); // TODO: separate coefs

			m_asm.movsx(mulInB, coefsPtr(pc));
		}

		bool setcondition = false;
		switch (instr.opType)
		{
		case kDMAC:
			{
				// mulInputA_24 = last_mulInputA_24 >> 7;
				auto last_mulInputA_24 = m_pool.get(m_data.last_mulInputA_24, Access::Read);
				m_asm.movsxd(mulInA, last_mulInputA_24.r32());
				m_asm.sar(mulInA, 7);
				if (lastMul30)
				{
					// mulInputB_24 = (last_mulInputB_24 >> 9) & 0x7f;
					auto last_mulInputB_24 = m_pool.get(m_data.last_mulInputB_24, Access::Read);
					m_asm.movsxd(mulInB, last_mulInputB_24.r32());
					m_asm.sar(mulInB, 9);
					m_asm.and_(mulInB, 0x7f);
				}
			}
			break;
		case kInterp:
			// mulInputA_24 = (~mulInputA_24 & 0x7fffff);
			m_asm.not_(mulInA);
			m_asm.and_(mulInA, 0x7fffff);
			break;

		case kStoreIRAM:
			{
				// iram[mempos] = mulInputA_24 = sat(readAcc);
				m_asm.mov(mulInA, tempA);
				m_asm.mov(iramPtr(tempB), tempA.r32());
			}
			break;
		case kReadGRAM:
			{
				// mulInputA_24 = gram[mempos];
				m_asm.movsxd(mulInA, gramPtr(tempB));
			}
			break;
		case kStoreGRAM:
			{
				// gram[mempos] = mulInputA_24 = sat(readAcc);
				m_asm.mov(mulInA, tempA);
				m_asm.mov(gramPtr(tempB), tempA.r32());
			}
			break;
		case kStoreIRAMUnsat:
			{
				// iram[mempos] = mulInputA_24 = readAcc;
				m_asm.mov(mulInA, tempA);
				m_asm.mov(iramPtr(tempB), tempA.r32());
			}
			break;
		case kStoreIRAMRect:
			{
				// iram[mempos] = mulInputA_24 = std::max(0, sat(readAcc));
				m_asm.mov(mulInA, tempA);
				m_asm.mov(tempA, 0);
				m_asm.cmp(mulInA.r32(), tempA.r32());
				m_asm.cmov(CondCode::kL, mulInA, tempA);
				m_asm.mov(iramPtr(tempB), mulInA.r32());
			}
			break;

		case kWriteEramVarOffset:
			{
				// eram.eramVarOffset = readAcc;
				auto eramVarOffset = m_pool.get(m_data.eramVarOffset, Access::Write);
				m_asm.mov(eramVarOffset, tempA);
			}
			break;
		case kWriteHost:
			{
				// *((int32_t*)&readback_regs) = sat(readAcc);
				m_asm.nop();
				m_asm.mov(hostregPtr(), tempA.r32());
				m_asm.nop();
			}
			break;
		case kWriteEramWriteLatch:
			// eram.eramWriteLatch = sat(readAcc);
			{
				auto eramWriteLatch = m_pool.get(m_data.eramWriteLatch, Access::Write);
				m_asm.mov(eramWriteLatch, tempA);
			}
			break;
		case kReadEramReadLatch:
			{
				// mulInputA_24 = eram.eramReadLatch;
				{
					auto eramReadLatch = m_pool.get(m_data.eramReadLatch, Access::Read);
					m_asm.movsxd(mulInA, eramReadLatch.r32());
				}

				// writeIRAM(mulInputA_24, instr.mem | 0xf0);
				auto iramPos = m_pool.get(m_data.iramPos, Access::Read);
				m_asm.lea(tempB, ptr(iramPos, static_cast<int32_t>(instr.mem | 0xf0)));// tempB = instr.mem + iramPos
				m_asm.and_(tempB, 0xff); // tempB &= 0xff
				m_asm.mov(iramPtr(tempB), mulInA.r32());
			}
			break;
		case kWriteMulCoef:
			{
				// mulcoeffs[(instr.mem >> 1) & 7] = sat(readAcc);
				const auto index = static_cast<int32_t>((instr.mem >> 1) & 7);
				m_asm.mov(mulcoefsPtr(index), tempA.r32());
			}
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
						m_asm.cmp(mulInA.r32(), 0);
						m_asm.mov(tempA , 0x007fffff);
						m_asm.mov(mulInA, 0xff800000);
						m_asm.cmov(CondCode::kGE, mulInA, tempA);
					}
					// iram[mempos] = mulInputA_24;
					m_asm.mov(iramPtr(tempB), mulInA.r32());
				}

				if ((instr.coef >> 5) == 6)
				{
					// mulInputB_24 = (shared.eram.eramVarOffset << 11) & 0x7fffff;
					auto eramVarOffset = m_pool.get(m_data.eramVarOffset, Access::Read);
					m_asm.movsxd(mulInB, eramVarOffset.r32());
					m_asm.shl(mulInB, 11);
					m_asm.and_(mulInB, 0x7fffff);
				}
				else if ((instr.coef >> 5) == 7)
				{
					// mulInputB_24 = shared.mulcoeffs[5];
					m_asm.movsxd(mulInB, mulcoefsPtr(5));
				}
				else
				{
					// mulInputB_24 = shared.mulcoeffs[coef >> 5];
					const auto index = (instr.coef >> 5) & 7;
					m_asm.movsxd(mulInB, mulcoefsPtr(index));
				}

				if ((instr.coef & 8) && !weird)
				{
					// mulInputB_24 *= -1;
					m_asm.neg(mulInB);
				}

				if ((instr.coef & 16) && !weird)
				{
					// if (mulInputB_24 >= 0) mulInputB_24 = (~mulInputB_24 & 0x7fffff);
					// else mulInputB_24 = ~(mulInputB_24 & 0x7fffff);
					m_asm.mov(tempA, mulInB);
					m_asm.not_(tempA);
					m_asm.and_(tempA.r32(), 0x7fffff);

					auto temp = m_pool.getTemp();
					m_asm.mov(temp, mulInB);
					m_asm.and_(temp, 0x7fffff);
					m_asm.not_(temp);

					m_asm.cmp(mulInB.r32(), 0);

					m_asm.cmov(CondCode::kGE, mulInB, tempA);
					m_asm.cmov(CondCode::kL, mulInB, temp);
				}

				// last_mulInputB_24 = mulInputB_24;
				{
					auto last_mulInputB_24 = m_pool.get(m_data.last_mulInputB_24, Access::Write);
					m_asm.mov(last_mulInputB_24, mulInB);
				}

				// mulInputB_24 >>= 16;
				m_asm.sar(mulInB, 16);
			}
			break;

		case kInterpStorePos:
			{
				// iram[mempos] = mulInputA_24 = sat(readAcc);
				m_asm.mov(mulInA, tempA);
				m_asm.mov(iramPtr(tempB), mulInA.r32());

				// if (mulInputA_24 >= 0) mulInputA_24 = ~mulInputA_24;
				m_asm.not_(mulInA);
				m_asm.cmp(tempA.r32(), 0);
				m_asm.cmov(CondCode::kL, mulInA, tempA);

				// mulInputA_24 &= 0x7fffff;
				m_asm.and_(mulInA, 0x7fffff);
			}
			break;
		case kInterpStoreNeg:
			{
				// iram[mempos] = mulInputA_24 = sat(readAcc);
				m_asm.mov(mulInA, tempA);
				m_asm.mov(iramPtr(tempB), mulInA.r32());

				// if (mulInputA_24 < 0) mulInputA_24 = ~mulInputA_24;
				m_asm.not_(mulInA);
				m_asm.cmp(tempA.r32(), 0);
				m_asm.cmov(CondCode::kGE, mulInA, tempA);

				// mulInputA_24 &= 0x7fffff;
				m_asm.and_(mulInA, 0x7fffff);
			}
			break;

		default:
			break;
		}

		const bool clr = instr.m_access.clr;
		if (!instr.m_access.nomac && instr.m_access.srcReg != -1 && instr.m_access.destReg != -1)
		{
			// last_mulInputA_24 = mulInputA_24;
			{
				auto last_mulInputA_24 = m_pool.get(m_data.last_mulInputA_24, Access::Write);
				m_asm.mov(last_mulInputA_24, mulInA);
			}

			int64_t srcAcc = instr.m_access.srcReg;
			int64_t destAcc = instr.m_access.destReg;

			// result = (int64_t)se<24>(mulInputA_24) * (int64_t) mulInputB_24;
			m_asm.shl(mulInA, 64 - 24);
			m_asm.sar(mulInA, 64 - 24);
			m_asm.mov(tempA, mulInA);
			m_asm.imul(tempA, mulInB);

			// result >>= instr.shiftAmount;
			// m_asm.asr(tempA, tempA, instr.shiftAmount);
			{
				m_asm.movsx(mulInB, shiftPtr(pc));
				m_asm.mov(rcx, mulInB);
				m_asm.sar(tempA, rcx.r8());
			}

			if (!clr)
			{
				// result += *srcAcc;
				auto acc = m_pool.get(&m_data.coreData->accs[srcAcc], Access::Read);
				m_asm.add(tempA, acc);
			}

			{
				auto acc = m_pool.get(&m_data.coreData->accs[destAcc], Access::Write);
				// *destAcc = result;
				m_asm.mov(acc, tempA);
			}
		}
		else
		{
			// last_mulInputA_24 = 0;
			auto last_mulInputA_24 = m_pool.get(m_data.last_mulInputA_24, Access::Write);
			m_asm.xor_(last_mulInputA_24.r32(), last_mulInputA_24.r32());
		}
	}

	Mem EspJitX64::iramPtr(const Gpq& offset) const
	{
		return ptr(g_regBasePtr, offset, 2, m_pool.getPointerOffset(m_data.iram), 4);
	}

	Mem EspJitX64::eramPtr(const Gpq& offset) const
	{
		return ptr(g_regBasePtr, offset, 2, m_pool.getPointerOffset(m_data.coreData->eramPtr), 4);
	}

	Mem EspJitX64::gramPtr(const Gpq& offset) const
	{
		return ptr(g_regBasePtr, offset, 2, m_pool.getPointerOffset(m_data.gram), 4);
	}

	Mem EspJitX64::coefsPtr(const uint32_t index) const
	{
		assert(index < std::size(m_data.coreData->coefs));
		return ptr(g_regBasePtr, m_pool.getPointerOffset(&m_data.coreData->coefs[index]), 1);
	}

	Mem EspJitX64::mulcoefsPtr(const uint32_t index) const
	{
		assert(index < std::size(m_data.coreData->mulcoeffs));
		return ptr(g_regBasePtr, m_pool.getPointerOffset(&m_data.coreData->mulcoeffs[index]), 4);
	}

	Mem EspJitX64::shiftPtr(const uint32_t index) const
	{
		assert(index < std::size(m_data.coreData->shiftAmounts));
		return ptr(g_regBasePtr, m_pool.getPointerOffset(&m_data.coreData->shiftAmounts[index]), 1);
	}

	Mem EspJitX64::hostregPtr() const
	{
		return ptr(g_regBasePtr, m_pool.getPointerOffset(m_data.coreData->hostRegPtr), 4);
	}
}
