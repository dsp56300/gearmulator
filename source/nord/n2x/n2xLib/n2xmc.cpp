#include "n2xmc.h"

#include <cassert>

#include "n2xrom.h"

namespace n2x
{
	Microcontroller::Microcontroller(const Rom& _rom)
	{
		if(!_rom.isValid())
			return;

		m_rom = _rom.data();
		m_ram.fill(0);

		dumpAssembly("n2x_68k.asm", g_romAddress, g_romSize);

		reset();

		setPC(g_pcInitial);
	}

	uint32_t Microcontroller::read32(uint32_t _addr)
	{
		return Mc68k::read32(_addr);
	}

	uint16_t Microcontroller::readImm16(const uint32_t _addr)
	{
		if(_addr < g_romSize)											return readW(m_rom.data(), _addr);
		if(_addr >= g_ramAddress && _addr < g_ramAddress + g_ramSize)	return readW(m_ram.data(), _addr - g_ramAddress);

		const auto pa = static_cast<mc68k::PeriphAddress>(_addr);

		assert(!m_hdi08A.isInRange(pa));
		assert(!m_hdi08B.isInRange(pa));

		return 0;
	}

	uint16_t Microcontroller::read16(const uint32_t _addr)
	{
		if(_addr < g_romSize)											return readW(m_rom.data(), _addr);
		if(_addr >= g_ramAddress && _addr < g_ramAddress + g_ramSize)	return readW(m_ram.data(), _addr - g_ramAddress);

		const auto pa = static_cast<mc68k::PeriphAddress>(_addr);

		if(m_hdi08A.isInRange(pa))										return m_hdi08A.read16(pa);
		if(m_hdi08B.isInRange(pa))										return m_hdi08B.read16(pa);

		return Mc68k::read16(_addr);
	}

	uint8_t Microcontroller::read8(const uint32_t _addr)
	{
		if(_addr < g_romSize)											return m_rom[_addr];
		if(_addr >= g_ramAddress && _addr < g_ramAddress + g_ramSize)	return m_ram[_addr - g_ramAddress];

		const auto pa = static_cast<mc68k::PeriphAddress>(_addr);

		if(m_hdi08A.isInRange(pa))										return m_hdi08A.read8(pa);
		if(m_hdi08B.isInRange(pa))										return m_hdi08B.read8(pa);

		return Mc68k::read8(_addr);
	}

	void Microcontroller::write16(const uint32_t _addr, const uint16_t _val)
	{
		if(_addr < g_romSize)
		{
			assert(false);
			writeW(m_rom.data(), _addr, _val);
			return;
		}
		if(_addr >= g_ramAddress && _addr < g_ramAddress + g_ramSize)
		{
			writeW(m_ram.data(), _addr - g_ramAddress, _val);
			return;
		}

		const auto pa = static_cast<mc68k::PeriphAddress>(_addr);

		if(m_hdi08A.isInRange(pa))
		{
			m_hdi08A.write16(pa, _val);
			return;
		}

		if(m_hdi08B.isInRange(pa))
		{
			m_hdi08B.write16(pa, _val);
			return;
		}

		Mc68k::write16(_addr, _val);
	}

	void Microcontroller::write8(const uint32_t _addr, const uint8_t _val)
	{
		if(_addr < g_romSize)
		{
			assert(false);
			m_rom[_addr] = _val;
			return;
		}
		if(_addr >= g_ramAddress && _addr < g_ramAddress + g_ramSize)
		{
			m_ram[_addr - g_ramAddress] = _val;
			return;
		}

		const auto pa = static_cast<mc68k::PeriphAddress>(_addr);

		if(m_hdi08A.isInRange(pa))
		{
			m_hdi08A.write8(pa, _val);
			return;
		}

		if(m_hdi08B.isInRange(pa))
		{
			m_hdi08B.write8(pa, _val);
			return;
		}

		Mc68k::write8(_addr, _val);
	}

	uint32_t Microcontroller::exec()
	{
		const auto pc = getPC();
		if(pc >= g_ramAddress)
		{
			if(pc == 0x1000c8)
				dumpAssembly("nl2x_68k_ram.asm", g_ramAddress, g_ramSize);
		}
		const auto cycles = Mc68k::exec();

		m_hdi08A.exec(cycles);
		m_hdi08B.exec(cycles);

		return cycles;
	}
}
