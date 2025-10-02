#include "esp_jit_x64_pool.h"

#include <cassert>
#include <stdexcept>

#include "esp_jit_x64_types.h"

namespace esp
{
	PooledReg::PooledReg(RegPoolX64& _pool, const asmjit::x86::Gpq& reg, void* ptr, const bool _read, const bool _write, uint32_t dataSize)
		: m_pool(_pool)
		, m_reg(reg)
		, m_ptr(ptr)
		, m_read(false)
		, m_write(false)
		, m_dataSize(dataSize)
	{
		if (isTemp())
			assert(!m_read && !m_write && "temporary pointers cannot be read or written");

		if (_read)
			read();
		if (_write)
			write();
	}

	PooledReg::~PooledReg()
	{
		if (m_write)
			store();
		if (isTemp())
			m_pool.releaseTemp(static_cast<uint8_t>(reinterpret_cast<uintptr_t>(m_ptr)));

		m_reg = {};
		m_write = m_read = false;
		m_dataSize = 0;
		m_ptr = nullptr;
	}

	void PooledReg::setLocked(bool _locked)
	{
		if (_locked)
		{
			++m_locked;
		}
		else
		{
			assert(m_locked > 0 && "unbalanced unlock");
			--m_locked;
		}
	}

	void PooledReg::load()
	{
		assert(!isTemp() && "temporary pointers cannot be loaded");

		if (m_loaded)
			return;

		assert(!m_write && "load after write");

		if (m_dataSize == 4)
			m_pool.getAsm().movsxd(m_reg, getPointer());
		else
			m_pool.getAsm().mov(getRegForDataSize(), getPointer());
		m_loaded = true;
	}

	void PooledReg::store()
	{
		assert(!isTemp() && "temporary pointers cannot be stored");

		if (!m_loaded)
			return;
		m_pool.getAsm().mov(getPointer(), getRegForDataSize());
		m_loaded = false;
	}

	void PooledReg::read()
	{
		if (m_read)
			return;
		load();
		m_read = true;
	}

	void PooledReg::write()
	{
		if (m_write)
			return;

		if (m_read)
			load();
		else
			m_loaded = true; // mark as loaded so store() will write it back and read() will not overwrite our already written value

		m_write = true;
	}

	int32_t PooledReg::getPointerOffset(void* _base, void* _derived)
	{
		std::ptrdiff_t offset = static_cast<uint8_t*>(_derived) - static_cast<uint8_t*>(_base);

		constexpr auto minValue = std::numeric_limits<int32_t>::lowest();
		constexpr auto maxValue = std::numeric_limits<int32_t>::max();

		assert(offset >= minValue && offset <= maxValue && "address is too far away");

		return static_cast<int32_t>(offset);
	}

	asmjit::x86::Mem PooledReg::getPointer() const
	{
		const auto offset = getPointerOffset(m_pool.getBasePtr(), m_ptr);

		return asmjit::x86::ptr(m_pool.getBaseReg(), offset, m_dataSize);
	}

	asmjit::x86::Gp PooledReg::getRegForDataSize() const
	{
		switch (m_dataSize)
		{
		case 8:
			return m_reg;
		case 4:
			return m_reg.r32();
		case 2:
			return m_reg.r16();
		case 1:
			return m_reg.r8();
		default:
			assert(false && "unknown data size");
			return {};
		}
	}

	ScopedPooledReg::ScopedPooledReg(PooledReg* reg) : m_reg(reg)
	{
		m_reg->setLocked(true);
	}

	ScopedPooledReg::~ScopedPooledReg()
	{
		m_reg->setLocked(false);
	}

	ScopedPooledReg::operator asmjit::x86::Gpq() const
	{
		return *m_reg;
	}

	asmjit::x86::Gpq ScopedPooledReg::r64() const
	{
		return m_reg->r64();
	}

	asmjit::x86::Gpd ScopedPooledReg::r32() const
	{
		return m_reg->r32();
	}

	RegPoolX64::RegPoolX64(Asm& a, const Reg& baseReg, void* basePtr) : m_asm(a), m_baseReg(baseReg), m_basePtr(basePtr)
	{
		for (const auto& r : g_poolRegs)
		{
			m_pooledRegPool.push_back(std::make_unique<PooledReg>(*this, r, nullptr, false, false, 8));
			m_unusedRegs.push_back(r);
		}

		for (uint8_t i=0; i< PooledReg::MaximumTemps; ++i)
			m_unusedTemps.push_back(i + 1);
	}

	RegPoolX64::~RegPoolX64()
	{
		clear();
	}

	ScopedPooledReg RegPoolX64::getTemp()
	{
		if(m_unusedTemps.empty())
			throw std::runtime_error("Failed to allocate temporary register");

		const uint8_t index = m_unusedTemps.back();
		m_unusedTemps.pop_back();

		m_usedTemps.push_back(index);

		return get(reinterpret_cast<void*>(static_cast<uintptr_t>(index)), false, false, 8);  // NOLINT(performance-no-int-to-ptr)
	}

	int32_t RegPoolX64::getPointerOffset(void* ptr) const
	{
		return PooledReg::getPointerOffset(m_basePtr, ptr);
	}

	void RegPoolX64::releaseTemp(uint8_t index)
	{
		assert(!m_usedTemps.empty() && "no temps in use");

		if (m_usedTemps.back() == index)
		{
			m_usedTemps.pop_back();
			m_unusedTemps.push_back(index);
			return;
		}

		for (int i = static_cast<int>(m_usedTemps.size()) - 1; i>=0; --i)
		{
			if (m_usedTemps[i] != index)
				continue;
			m_usedTemps.erase(m_usedTemps.begin() + i);
			m_unusedTemps.push_back(index);
			return;
		}
		assert(false && "temp not found in used list");
	}

	void RegPoolX64::clear()
	{
		m_usedRegs.clear();
		m_unusedRegs.clear();
		m_usedTemps.clear();
		m_unusedTemps.clear();

		for (const auto& r : g_poolRegs)
			m_unusedRegs.push_back(r);

		for (uint8_t i=0; i< PooledReg::MaximumTemps; ++i)
			m_unusedTemps.push_back(i);
	}

	void RegPoolX64::checkUninit(const asmjit::x86::Gpq& reg) const
	{
		const auto label = m_asm.newLabel();
		m_asm.cmp(reg.r32(), 0xbadc0de);
		m_asm.jne(label);
		m_asm.int3();
		m_asm.bind(label);
	}

	ScopedPooledReg RegPoolX64::get(void* _ptr, bool _read, bool _write, uint32_t dataSize)
	{
		// check if already used, if so, return that one
		for (auto it = m_usedRegs.begin(); it != m_usedRegs.end(); ++it)
		{
			if (it->first != _ptr)
				continue;

			auto reg = std::move(it->second);

			if (_read)
				reg->read();

			if (_write)
				reg->write();

			auto* rPtr = reg.get();

			assert(reg->getDataSize() == dataSize && "data size mismatch");

			// reinsert at the end of the list to mark as most recently used
			m_usedRegs.erase(it);
			m_usedRegs.emplace_back(_ptr, std::move(reg));

			return ScopedPooledReg(rPtr);
		}

		auto returnUnused = [&]
		{
			auto reg = m_unusedRegs.front();
			m_unusedRegs.pop_front();

			assert(!m_pooledRegPool.empty() && "no pooled registers left");

			if (m_pooledRegPool.empty())
				throw std::runtime_error("Failed to allocate register");

			auto r = std::move(m_pooledRegPool.back());
			m_pooledRegPool.pop_back();

			auto* rPtr = r.get();

			new (rPtr) PooledReg(*this, reg, _ptr, _read, _write, dataSize); // placement new

			m_usedRegs.emplace_back(_ptr, std::move(r));

			return ScopedPooledReg(rPtr);
		};

		// need to allocate a new one. Do we have any free ones?
		if (!m_unusedRegs.empty())
			return returnUnused();

		// No we do not. Free the least recently used one that is not locked
		for (auto it = m_usedRegs.begin(); it != m_usedRegs.end(); ++it)
		{
			{
				auto& reg = it->second;

				if (reg->isLocked())
					continue;

				// found one to evict
				m_unusedRegs.push_back(*reg);						// put back into unused list

				reg->~PooledReg();									// call destructor
				m_pooledRegPool.push_back(std::move(it->second));	// put back into pool
				m_usedRegs.erase(it);								// remove from used list
			}

			return returnUnused();
		}

		assert(false && "no registers left in pool");
		throw std::runtime_error("Failed to allocate register");
	}
}
