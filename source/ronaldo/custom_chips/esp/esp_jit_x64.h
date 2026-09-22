#pragma once

#include "esp_jit.h"

#include "esp_jit_x64_pool.h"

namespace esp
{
	class EspJitX64 : public EspJitBase
	{
	public:
		using Asm = asmjit::x86::Builder;

		EspJitX64(Asm& a, const JitInputData& _data);

		void jitEnter() override;
		void jitExit() override;

		void eramRead(uint32_t _eramMask) override;
		void eramWrite(uint32_t _eramMask) override;
		void eramComputeAddr(uint32_t immOffset, bool highOffset, bool shouldUseVarOffset);

		void checkUninit(const asmjit::x86::Gpq& reg) const;
		void emitOp(uint32_t pc, const ESPOptInstr& instr, bool lastMul30);

	private:
		asmjit::x86::Mem iramPtr(const asmjit::x86::Gpq& offset) const;
		asmjit::x86::Mem eramPtr(const asmjit::x86::Gpq& offset) const;
		asmjit::x86::Mem gramPtr(const asmjit::x86::Gpq& offset) const;
		asmjit::x86::Mem coefsPtr(uint32_t index) const;
		asmjit::x86::Mem mulcoefsPtr(uint32_t index) const;
		asmjit::x86::Mem shiftPtr(uint32_t index) const;
		asmjit::x86::Mem hostregPtr() const;

		Asm& m_asm;
		JitInputData m_data;
		RegPoolX64 m_pool;
	};
}
