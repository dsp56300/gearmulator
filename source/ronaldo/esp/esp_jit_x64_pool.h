#pragma once

#include <deque>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "asmjit/x86/x86builder.h"

namespace esp
{
	enum class Access : uint8_t
	{
		Read,
		Write,
		ReadWrite
	};

	class RegPoolX64;

	class PooledReg
	{
	public:
		static constexpr uint32_t MaximumTemps = 16;

		PooledReg(RegPoolX64& _pool, const asmjit::x86::Gpq& reg, void* ptr, bool read, bool write, uint32_t dataSize);

		PooledReg(const PooledReg&) = delete;
		PooledReg(PooledReg&&) = default;

		PooledReg& operator=(const PooledReg&) = delete;
		PooledReg& operator=(PooledReg&&) = delete;

		~PooledReg();

		asmjit::x86::Gpq r64() { return m_reg; }
		asmjit::x86::Gpd r32() { return m_reg.r32(); }

		auto ptr() const { return m_ptr; }

		operator asmjit::x86::Gpq() { return m_reg; }

		void setLocked(bool _locked);
		bool isLocked() const { return m_locked; }

		void load();
		void store();

		bool read() const { return m_read; }
		bool written() const { return m_write; }

		void read();
		void write();

		uint32_t getDataSize() const { return m_dataSize; }

		static bool isTemp(void* _ptr) { return _ptr && reinterpret_cast<intptr_t>(_ptr) <= MaximumTemps; }
		bool isTemp() const { return isTemp(m_ptr); }

		static int32_t getPointerOffset(void* _base, void* _derived);

	private:
		asmjit::x86::Mem getPointer() const;
		asmjit::x86::Gp getRegForDataSize() const;

		RegPoolX64& m_pool;
		asmjit::x86::Gpq m_reg;
		void* m_ptr;
		uint32_t m_locked = false;
		bool m_read;
		bool m_write;
		uint32_t m_dataSize;
		bool m_loaded = false;
	};

	class ScopedPooledReg
	{
	public:
		explicit ScopedPooledReg(PooledReg* reg);
		ScopedPooledReg(const ScopedPooledReg&) = delete;
		ScopedPooledReg(ScopedPooledReg&&) = default;

		ScopedPooledReg& operator=(const ScopedPooledReg&) = default;
		ScopedPooledReg& operator=(ScopedPooledReg&&) = delete;

		~ScopedPooledReg();

		operator asmjit::x86::Gpq() const;

		asmjit::x86::Gpq r64() const;
		asmjit::x86::Gpd r32() const;

		bool isValid() const { return m_reg != nullptr; }

	private:
		PooledReg* m_reg;
	};

	class RegPoolX64
	{
	public:
		using Reg = asmjit::x86::Gpq;
		using Asm = asmjit::x86::Builder;
		using UsedReg = std::pair<void*, std::unique_ptr<PooledReg>>;

		RegPoolX64(Asm& a, const Reg& baseReg, void* basePtr);
		~RegPoolX64();

		template<typename T>
		ScopedPooledReg get(T* _ptr, bool _read, bool _write)
		{
			constexpr auto dataSize = sizeof(T);
			static_assert(
				dataSize == 8 || 
				dataSize == 4 /*||
				dataSize == 2 || 
				dataSize == 1*/, "unknown data type");
			return get(_ptr, _read, _write, static_cast<uint32_t>(dataSize));
		}
		template<typename T>
		ScopedPooledReg get(T* _ptr, Access _access)
		{
			return get(_ptr, _access != Access::Write, _access != Access::Read);
		}

		ScopedPooledReg getTemp();

		Asm& getAsm() { return m_asm; }	
		Reg getBaseReg() const { return m_baseReg; }
		void* getBasePtr() const { return m_basePtr; }

		int32_t getPointerOffset(void* ptr) const;

		void releaseTemp(uint8_t index);

		void clear();
		void checkUninit(const asmjit::x86::Gpq& reg) const;

	private:
		ScopedPooledReg get(void* _ptr, bool _read, bool _write, uint32_t dataSize);

		Asm& m_asm;
		Reg m_baseReg;
		void* m_basePtr;

		std::deque<UsedReg> m_usedRegs;
		std::deque<Reg> m_unusedRegs;

		std::vector<uint8_t> m_usedTemps;
		std::vector<uint8_t> m_unusedTemps;

		std::vector<std::unique_ptr<PooledReg>> m_pooledRegPool;
	};
}
