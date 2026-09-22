#pragma once

#include "esp_jit.h"

#include "asmjit/arm/a64builder.h"

namespace esp
{
	class EspJitArm64 : public EspJitBase
	{
	public:
		using Asm = asmjit::a64::Builder;

		EspJitArm64(Asm& a, const JitInputData& _data);

		void jitEnter() override;
		void jitExit() override;

		void eramRead(uint32_t eramMask) override;
		void eramWrite(uint32_t eramMask) override;
		void eramComputeAddr(uint32_t immOffset, bool highOffset, bool shouldUseVarOffset) override;

		void emitOp(uint32_t pc, const ESPOptInstr& instr, bool lastMul30) override;

	private:
		Asm& m_asm;
	};
}
