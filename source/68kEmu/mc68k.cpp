#include "mc68k.h"

namespace mc68k
{
	Mc68k::Mc68k() : m_qsm(*this)
	{
		irqMode = moira::IRQ_USER;
	}
	Mc68k::~Mc68k()	= default;

	void Mc68k::exec()
	{
		execute();

		m_gpt.exec();
		m_sim.exec();
		m_qsm.exec();
	}

	void Mc68k::injectInterrupt(uint8_t _vector, uint8_t _level)
	{
		m_pendingInterrupts[_level].push_back(_vector);
		raiseIPL();
	}

	void Mc68k::writeW(std::vector<uint8_t>& _buf, size_t _offset, uint16_t _value)
	{
		_buf[_offset] = _value >> 8;
		_buf[_offset+1] = _value & 0xff;
	}

	uint16_t Mc68k::readW(const std::vector<uint8_t>& _buf, size_t _offset)
	{
		const uint16_t a = _buf[_offset];
		const uint16_t b = _buf[_offset+1];

		return static_cast<uint16_t>(a << 8 | b);
	}

	moira::u8 Mc68k::read8(moira::u32 _addr)
	{
		const auto addr = static_cast<PeriphAddress>(_addr & g_peripheralMask);

		if(m_gpt.isInRange(addr))			return m_gpt.read8(addr);
		if(m_sim.isInRange(addr))			return m_sim.read8(addr);
		if(m_qsm.isInRange(addr))			return m_qsm.read8(addr);

		return 0;
	}

	moira::u16 Mc68k::read16(moira::u32 _addr)
	{
		const auto addr = static_cast<PeriphAddress>(_addr & g_peripheralMask);

		if(m_gpt.isInRange(addr))			return m_gpt.read16(addr);
		if(m_sim.isInRange(addr))			return m_sim.read16(addr);
		if(m_qsm.isInRange(addr))			return m_qsm.read16(addr);

		return 0;
	}

	void Mc68k::write8(moira::u32 _addr, moira::u8 _val)
	{
		const auto addr = static_cast<PeriphAddress>(_addr & g_peripheralMask);

		if(m_gpt.isInRange(addr))			m_gpt.write8(addr, _val);
		else if(m_sim.isInRange(addr))		m_sim.write8(addr, _val);
		else if(m_qsm.isInRange(addr))		m_qsm.write8(addr, _val);
	}

	void Mc68k::write16(moira::u32 _addr, moira::u16 _val)
	{
		const auto addr = static_cast<PeriphAddress>(_addr & g_peripheralMask);

		if(m_gpt.isInRange(addr))			m_gpt.write16(addr, _val);
		else if(m_sim.isInRange(addr))		m_sim.write16(addr, _val);
		else if(m_qsm.isInRange(addr))		m_qsm.write16(addr, _val);
	}

	moira::u16 Mc68k::readIrqUserVector(moira::u8 level) const
	{
		auto* self = const_cast<Mc68k*>(this);

		auto& vecs = self->m_pendingInterrupts[level];
		assert(!vecs.empty());
		const auto vec = vecs.front();
		vecs.pop_front();

		self->setIPL(0);
		self->raiseIPL();

		return vec;
	}

	void Mc68k::raiseIPL()
	{
		for(int i=static_cast<int>(m_pendingInterrupts.size())-1; i>static_cast<int>(ipl); --i)
		{
			if(!m_pendingInterrupts[i].empty())
			{
				setIPL(static_cast<uint8_t>(i));
			}
		}
	}
}
