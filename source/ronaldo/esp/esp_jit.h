#pragma once

#include <cstdint>
#include <memory>

struct CoreData;
struct ESPOptInstr;

namespace esp
{
	class EspJitBase
	{
	public:
		EspJitBase();

		virtual void jitEnter() = 0;
		virtual void jitExit() = 0;

		virtual void eramRead(uint32_t _eramMask) = 0;
		virtual void eramWrite(uint32_t _eramMask) = 0;
		virtual void eramComputeAddr(uint32_t immOffset, bool highOffset, bool shouldUseVarOffset) = 0;

		// nextIsDmac: the next *emitted* (non-nop) op of this core is a kDMAC, i.e. the only reader of
		// last_mulInputA_24 / last_mulInputB_24 follows immediately. Emitters may skip those writes otherwise.
		virtual void emitOp(uint32_t pc, const ESPOptInstr& instr, bool lastMul30, bool nextIsDmac) = 0;
	};

	struct JitInputData
	{
		CoreData* coreData;
		int32_t* iram;
		int32_t* gram;

		uint32_t* eramPos;
		uint32_t* iramPos;

		uint32_t* eramEffectiveAddr;
		int32_t* eramWriteLatchNext;

		int32_t* eramReadLatch;
		int32_t* eramWriteLatch;
		int32_t* eramVarOffset;

		int32_t* last_mulInputA_24;
		int32_t* last_mulInputB_24;
	};
}
