#include "mcf5206e.h"

#include <algorithm>
#include <cstdint>

// Register maps and behaviour from the MCF5206e User's Manual (UM): SIM section 8, chip selects 9,
// DRAM controller 11, UART 12, timers 14, register summary appendix A.

namespace coldfire
{
	namespace
	{
		uint32_t readBE(const uint8_t* _p, const uint32_t _size)
		{
			switch(_size)
			{
			case 1:		return _p[0];
			case 2:		return (static_cast<uint32_t>(_p[0]) << 8) | _p[1];
			default:	return (static_cast<uint32_t>(_p[0]) << 24) | (static_cast<uint32_t>(_p[1]) << 16) | (static_cast<uint32_t>(_p[2]) << 8) | _p[3];
			}
		}

		void writeBE(uint8_t* _p, const uint32_t _value, const uint32_t _size)
		{
			switch(_size)
			{
			case 1:
				_p[0] = static_cast<uint8_t>(_value);
				break;
			case 2:
				_p[0] = static_cast<uint8_t>(_value >> 8);
				_p[1] = static_cast<uint8_t>(_value);
				break;
			default:
				_p[0] = static_cast<uint8_t>(_value >> 24);
				_p[1] = static_cast<uint8_t>(_value >> 16);
				_p[2] = static_cast<uint8_t>(_value >> 8);
				_p[3] = static_cast<uint8_t>(_value);
				break;
			}
		}

		// byte _index (0 = most significant) of a register of _size bytes
		uint8_t regByte(const uint32_t _reg, const uint32_t _size, const uint32_t _index)
		{
			return static_cast<uint8_t>(_reg >> ((_size - 1 - _index) * 8));
		}

		template<typename T> void setRegByte(T& _reg, const uint32_t _index, const uint8_t _value)
		{
			constexpr uint32_t size = sizeof(T);
			const uint32_t shift = (size - 1 - _index) * 8;
			_reg = static_cast<T>((_reg & ~(static_cast<T>(0xff) << shift)) | (static_cast<T>(_value) << shift));
		}

		// interrupt sources, numbered like their IMR/IPR bits
		enum Source : uint32_t
		{
			SrcEint1 = 1, SrcEint4 = 4, SrcEint7 = 7, SrcSwt = 8, SrcTimer1 = 9, SrcTimer2 = 10, SrcMbus = 11,
			SrcUart1 = 12, SrcUart2 = 13, SrcDma0 = 14, SrcDma1 = 15
		};

		// time to an external bus cycle beyond the core's zero wait state timing. ponytail: one flat cost
		// per bus cycle, DRAM page hits/misses and burst fills are not modelled
		constexpr uint8_t g_dramWaitCycles = 3;
		constexpr uint8_t g_busCycleOverhead = 1;
	}

	// ---------------------------------------------------------------------------------------------------
	// RAM

	uint32_t Ram::read(const uint32_t _offset, const uint32_t _size)
	{
		const auto mask = static_cast<uint32_t>(m_data.size() - 1);
		if(_size == 1)
			return m_data[_offset & mask];
		uint32_t v = 0;
		for(uint32_t i = 0; i < _size; ++i)
			v = (v << 8) | m_data[(_offset + i) & mask];
		return v;
	}

	void Ram::write(const uint32_t _offset, const uint32_t _value, const uint32_t _size)
	{
		const auto mask = static_cast<uint32_t>(m_data.size() - 1);
		for(uint32_t i = 0; i < _size; ++i)
			m_data[(_offset + i) & mask] = static_cast<uint8_t>(_value >> ((_size - 1 - i) * 8));
	}

	// ---------------------------------------------------------------------------------------------------
	// UART

	void Uart::reset()
	{
		m_mr1 = m_mr2 = 0;
		m_mrPointer2 = false;
		m_csr = 0xdd;
		m_imr = 0;
		m_acr = 0;
		m_ivr = 0x0f;
		m_outputPort = 0;
		m_txEnabled = m_rxEnabled = false;
		m_txHoldingFull = m_txShifting = false;
		m_txRemaining = 0;
		m_rxFifo.clear();
		m_overrun = false;
		m_rxRemaining = 0;
	}

	uint32_t Uart::cyclesPerCharacter() const
	{
		// UM 12.3.1: baud = system clock / (32 * divider); a character is start bit, 5-8 data bits,
		// optional parity and the stop bits
		uint32_t divider = (static_cast<uint32_t>(m_bg1) << 8) | m_bg2;
		if(!divider)
			divider = 1;
		const uint32_t dataBits = 5 + (m_mr1 & 3);
		const uint32_t parity = ((m_mr1 >> 3) & 3) == 2 ? 0 : 1;	// PM = 10 is no parity
		const uint32_t stopBits = (m_mr2 & 0x0f) >= 8 ? 2 : 1;
		const uint32_t bits = 1 + dataBits + parity + stopBits;
		return bits * 32 * divider;
	}

	uint8_t Uart::usr() const
	{
		uint8_t s = 0;
		if(!m_rxFifo.empty())
			s |= 0x01;						// RxRDY
		if(m_rxFifo.size() >= 3)
			s |= 0x02;						// FFULL
		if(m_txEnabled && !m_txHoldingFull)
			s |= 0x04;						// TxRDY
		if(m_txEnabled && !m_txHoldingFull && !m_txShifting)
			s |= 0x08;						// TxEMP
		if(m_overrun)
			s |= 0x10;						// OE
		return s;
	}

	uint8_t Uart::isr() const
	{
		const uint8_t s = usr();
		uint8_t r = 0;
		if(s & 0x04)
			r |= 0x01;						// TxRDY
		// UMR1 RxIRQ selects FFULL or RxRDY as the receiver interrupt
		if((m_mr1 & 0x40) ? (s & 0x02) : (s & 0x01))
			r |= 0x02;
		return r;
	}

	uint8_t Uart::read(const uint32_t _reg)
	{
		switch(_reg)
		{
		case 0x00:
			{
				const uint8_t v = m_mrPointer2 ? m_mr2 : m_mr1;
				m_mrPointer2 = true;
				return v;
			}
		case 0x04:
			return usr();
		case 0x0c:
			{
				if(m_rxFifo.empty())
					return 0xff;
				const uint8_t v = m_rxFifo.front();
				m_rxFifo.pop_front();
				return v;
			}
		case 0x10:
			return 0x0f;	// UIPCR: CTS state, no change
		case 0x14:
			return isr();
		case 0x18:
			return m_bg1;
		case 0x1c:
			return m_bg2;
		case 0x30:
			return m_ivr;
		case 0x34:
			return 0xfe;	// UIP: CTS asserted (low)
		default:
			return 0xff;
		}
	}

	void Uart::write(const uint32_t _reg, const uint8_t _value)
	{
		switch(_reg)
		{
		case 0x00:
			if(m_mrPointer2)
				m_mr2 = _value;
			else
				m_mr1 = _value;
			m_mrPointer2 = true;
			break;
		case 0x04:
			m_csr = _value;
			break;
		case 0x08:
			switch((_value >> 4) & 7)
			{
			case 1: m_mrPointer2 = false; break;
			case 2: m_rxEnabled = false; m_rxFifo.clear(); m_overrun = false; m_rxRemaining = 0; break;
			case 3: m_txEnabled = false; m_txHoldingFull = false; m_txShifting = false; m_txRemaining = 0; break;
			case 4: m_overrun = false; break;
			default: break;
			}
			switch((_value >> 2) & 3)
			{
			case 1: m_txEnabled = true; break;
			case 2: m_txEnabled = false; break;
			default: break;
			}
			switch(_value & 3)
			{
			case 1: m_rxEnabled = true; break;
			case 2: m_rxEnabled = false; break;
			default: break;
			}
			break;
		case 0x0c:
			if(!m_txEnabled)
				break;
			m_txHolding = _value;
			m_txHoldingFull = true;
			if(!m_txShifting)
			{
				m_txShift = m_txHolding;
				m_txHoldingFull = false;
				m_txShifting = true;
				m_txRemaining = cyclesPerCharacter();
			}
			break;
		case 0x10:
			m_acr = _value;
			break;
		case 0x14:
			m_imr = _value;
			break;
		case 0x18:
			m_bg1 = _value;
			break;
		case 0x1c:
			m_bg2 = _value;
			break;
		case 0x30:
			m_ivr = _value;
			break;
		case 0x38:
			m_outputPort |= (_value & 1);
			break;
		case 0x3c:
			m_outputPort &= ~(_value & 1);
			break;
		default:
			break;
		}
	}

	void Uart::advance(const uint32_t _cycles)
	{
		if(m_txShifting)
		{
			if(m_txRemaining > _cycles)
			{
				m_txRemaining -= _cycles;
			}
			else
			{
				const uint8_t b = m_txShift;
				if(m_txHoldingFull)
				{
					m_txShift = m_txHolding;
					m_txHoldingFull = false;
					m_txRemaining = cyclesPerCharacter();
				}
				else
				{
					m_txShifting = false;
					m_txRemaining = 0;
				}
				if(m_txCallback)
					m_txCallback(b);
			}
		}

		if(!m_rxPending.empty())
		{
			if(!m_rxRemaining)
				m_rxRemaining = cyclesPerCharacter();

			if(m_rxRemaining > _cycles)
			{
				m_rxRemaining -= _cycles;
			}
			else
			{
				m_rxRemaining = 0;
				const uint8_t b = m_rxPending.front();
				m_rxPending.pop_front();
				if(m_rxEnabled)
				{
					if(m_rxFifo.size() < 3)
						m_rxFifo.push_back(b);
					else
						m_overrun = true;
				}
			}
		}
	}

	uint32_t Uart::cyclesToNextEvent() const
	{
		uint32_t next = UINT32_MAX;
		if(m_txShifting)
			next = m_txRemaining;
		if(!m_rxPending.empty())
			next = std::min(next, m_rxRemaining ? m_rxRemaining : cyclesPerCharacter());
		return next;
	}

	// ---------------------------------------------------------------------------------------------------
	// timer

	void Timer::reset()
	{
		m_tmr = 0;
		m_trr = 0xffff;
		m_tcr = 0;
		m_tcn = 0;
		m_ter = 0;
		m_prescale = 0;
	}

	uint16_t Timer::read16(const uint32_t _reg) const
	{
		switch(_reg)
		{
		case 0x0: return m_tmr;
		case 0x4: return m_trr;
		case 0x8: return m_tcr;
		case 0xc: return m_tcn;
		default: return 0;
		}
	}

	uint8_t Timer::read8(const uint32_t _reg) const
	{
		if(_reg == 0x11)
			return m_ter;
		const uint16_t w = read16(_reg & ~1u);
		return static_cast<uint8_t>((_reg & 1) ? w : (w >> 8));
	}

	void Timer::write16(const uint32_t _reg, const uint16_t _value)
	{
		switch(_reg)
		{
		case 0x0:
			// UM 14.4.1.1: RST = 0 resets the timer, including the prescaler and TER
			m_tmr = _value;
			if(!(_value & 1))
			{
				m_tcn = 0;
				m_ter = 0;
				m_prescale = 0;
			}
			break;
		case 0x4: m_trr = _value; break;
		case 0xc: m_tcn = 0; break;	// any write clears the counter
		default: break;
		}
	}

	void Timer::write8(const uint32_t _reg, const uint8_t _value)
	{
		if(_reg == 0x11)
		{
			m_ter &= ~_value;	// write one to clear
			return;
		}
		uint16_t w = read16(_reg & ~1u);
		if(_reg & 1)
			w = static_cast<uint16_t>((w & 0xff00) | _value);
		else
			w = static_cast<uint16_t>((w & 0x00ff) | (_value << 8));
		write16(_reg & ~1u, w);
	}

	uint32_t Timer::divider() const
	{
		if(!(m_tmr & 1))
			return 0;

		uint32_t div;
		switch((m_tmr >> 1) & 3)
		{
		case 1: div = 1; break;
		case 2: div = 16; break;
		default: return 0;	// stopped or TIN, which is not connected
		}
		return div * ((m_tmr >> 8) + 1u);
	}

	uint32_t Timer::cyclesToNextEvent() const
	{
		const uint32_t div = divider();
		if(!div)
			return UINT32_MAX;

		// advance() compares TCN with TRR at each count, before incrementing it
		const uint32_t counts = ((m_trr - m_tcn) & 0xffffu) + 1;
		return counts * div - m_prescale;
	}

	void Timer::advance(const uint32_t _cycles)
	{
		const uint32_t div = divider();
		if(!div)
			return;

		m_prescale += _cycles;
		while(m_prescale >= div)
		{
			m_prescale -= div;

			if(m_tcn == m_trr)
			{
				m_ter |= 0x02;	// REF
				if(m_tmr & 0x08)
				{
					m_tcn = 0;	// FRR: restart
					continue;
				}
			}
			++m_tcn;
		}
	}

	bool Timer::interruptRequest() const
	{
		return ((m_ter & 0x02) && (m_tmr & 0x10)) || ((m_ter & 0x01) && (m_tmr & 0xc0));
	}

	// ---------------------------------------------------------------------------------------------------
	// MCF5206e

	Mcf5206e::Mcf5206e(const uint32_t _systemClockHz) : m_cpu(*this), m_systemClock(_systemClockHz), m_pages(65536)
	{
		for(auto& u : m_uarts)
		{
			u.setSystemClock(_systemClockHz);
			u.setWakeup(&m_nextPeripheralEvent);
		}
		resetModules();
	}

	void Mcf5206e::attach(const Region _region, Device* _device)
	{
		m_devices[static_cast<size_t>(_region)] = _device;
		rebuildMemoryMap();
	}

	void Mcf5206e::resetModules()
	{
		m_mbar = 0;
		m_rambar = 0;
		m_cacr = 0;
		m_acr0 = m_acr1 = 0;

		m_simr = 0xc0;
		m_icr = {0x00, 0x04, 0x08, 0x0c, 0x10, 0x14, 0x18, 0x1c, 0x1c, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00};
		m_imr = 0x3ffe;
		m_sypcr = 0;
		m_sypcrWritten = false;
		m_swivr = 0x0f;
		m_swsrLast = 0;
		m_par = 0;
		m_watchdogCounter = 0;

		m_csar[0] = 0;
		m_csmr[0] = 0;
		m_cscr[0] = 0x3d5f;	// AA enabled with 15 wait states, 16 bit port: IRQ7 and IRQ4 high at reset
		for(uint32_t i = 1; i < 8; ++i)
			m_cscr[i] &= ~0x021fu;	// BRST, ASET, WRAH, RDAH, WR, RD cleared
		m_globalChipSelect = true;
		m_dmcr = 0;
		m_dccr = {0, 0};

		m_ppddr = 0;
		m_ppdat = 0;

		for(auto& u : m_uarts)
			u.reset();
		for(auto& t : m_timers)
			t.reset();

		m_peripheralCycles = 0;
		m_nextPeripheralEvent = 0;

		rebuildMemoryMap();
	}

	void Mcf5206e::reset()
	{
		resetModules();
		m_rsr = 0x80;
		m_cpu.reset();
	}

	void Mcf5206e::watchdogReset()
	{
		resetModules();
		m_rsr = 0x20;
		if(m_watchdogReset)
			m_watchdogReset();
		m_cpu.reset();
	}

	void Mcf5206e::setMbar(const uint32_t _value)
	{
		m_mbar = _value;
	}

	void Mcf5206e::setRambar(const uint32_t _value)
	{
		m_rambar = _value;
	}

	void Mcf5206e::writeControlRegister(const uint16_t _reg, const uint32_t _value)
	{
		switch(_reg)
		{
		case 0x002: m_cacr = _value; break;
		case 0x004: m_acr0 = _value; break;
		case 0x005: m_acr1 = _value; break;
		case 0xc04: m_rambar = _value; break;
		case 0xc0f: m_mbar = _value; break;
		default: break;
		}
	}

	uint32_t Mcf5206e::exec()
	{
		const uint32_t cycles = m_cpu.step();

		m_peripheralCycles += cycles;
		if(m_peripheralCycles >= m_nextPeripheralEvent)
			syncPeripherals();

		return cycles;
	}

	void Mcf5206e::syncPeripherals()
	{
		const uint32_t cycles = m_peripheralCycles;
		m_peripheralCycles = 0;

		m_uarts[0].advance(cycles);
		m_uarts[1].advance(cycles);
		m_timers[0].advance(cycles);
		m_timers[1].advance(cycles);
		advanceWatchdog(cycles);

		updateInterrupts();

		const uint32_t watchdog = (m_sypcr & 0x80) ? watchdogTimeout() - m_watchdogCounter : UINT32_MAX;
		m_nextPeripheralEvent = std::min({m_uarts[0].cyclesToNextEvent(), m_uarts[1].cyclesToNextEvent(),
			m_timers[0].cyclesToNextEvent(), m_timers[1].cyclesToNextEvent(), watchdog});
	}

	void Mcf5206e::setExternalInterrupt(const uint32_t _level, const bool _asserted)
	{
		if(_asserted)
			m_externalIrq |= static_cast<uint16_t>(1u << _level);
		else
			m_externalIrq &= static_cast<uint16_t>(~(1u << _level));
		m_nextPeripheralEvent = 0;
	}

	// ---------------------------------------------------------------------------------------------------
	// interrupts

	void Mcf5206e::updateInterrupts()
	{
		uint16_t pending = m_externalIrq & 0x00fe;

		if(m_timers[0].interruptRequest())	pending |= 1u << SrcTimer1;
		if(m_timers[1].interruptRequest())	pending |= 1u << SrcTimer2;
		if(m_uarts[0].interruptRequest())	pending |= 1u << SrcUart1;
		if(m_uarts[1].interruptRequest())	pending |= 1u << SrcUart2;

		// preserve a watchdog interrupt until it is acknowledged
		pending |= m_ipr & (1u << SrcSwt);

		m_ipr = pending;

		const uint16_t active = pending & ~m_imr;

		uint8_t level = 0;
		for(uint32_t src = 1; src < 16; ++src)
		{
			if(!(active & (1u << src)))
				continue;
			const uint8_t l = src <= SrcSwt ? static_cast<uint8_t>(src <= 7 ? src : 7) : static_cast<uint8_t>((m_icr[src] >> 2) & 7);
			if(l > level)
				level = l;
		}

		m_cpu.setInterruptLevel(level);
	}

	uint8_t Mcf5206e::interruptAcknowledge(const uint8_t _level)
	{
		const uint8_t vector = acknowledgeInterrupt(_level);
		if(m_interruptMonitor)
			m_interruptMonitor(_level, vector);
		m_nextPeripheralEvent = 0;	// the acknowledge may have cleared a request
		return vector;
	}

	uint8_t Mcf5206e::acknowledgeInterrupt(const uint8_t _level)
	{
		const uint16_t active = m_ipr & ~m_imr;

		// the source at this level with the highest priority (IP) answers
		int32_t best = -1;
		int32_t bestPrio = -1;

		for(uint32_t src = 1; src < 16; ++src)
		{
			if(!(active & (1u << src)))
				continue;
			const uint8_t l = src <= SrcSwt ? static_cast<uint8_t>(src <= 7 ? src : 7) : static_cast<uint8_t>((m_icr[src] >> 2) & 7);
			if(l != _level)
				continue;
			const int32_t prio = m_icr[src] & 3;
			if(prio > bestPrio)
			{
				bestPrio = prio;
				best = static_cast<int32_t>(src);
			}
		}

		if(best < 0)
			return Cpu::VecSpuriousInterrupt;

		const auto src = static_cast<uint32_t>(best);

		if(src == SrcSwt)
		{
			m_ipr &= ~(1u << SrcSwt);
			return m_swivr;
		}

		// timers and MBUS always autovector (UM 8.3.2.3)
		const bool avec = (m_icr[src] & 0x80) || src == SrcTimer1 || src == SrcTimer2 || src == SrcMbus;
		if(avec)
			return static_cast<uint8_t>(Cpu::VecAutovector1 + _level - 1);

		switch(src)
		{
		case SrcUart1: return m_uarts[0].getVector();
		case SrcUart2: return m_uarts[1].getVector();
		case SrcDma0: return m_dma[0x14];
		case SrcDma1: return m_dma[0x54];
		default: return 0x0f;	// external device without a vector: uninitialized interrupt
		}
	}

	// ---------------------------------------------------------------------------------------------------
	// watchdog

	uint32_t Mcf5206e::watchdogTimeout() const
	{
		const uint32_t swt = (m_sypcr >> 3) & 3;
		uint32_t timeout = 1u << (9 + swt * 2);
		if(m_sypcr & 0x20)
			timeout <<= 9;
		return timeout;
	}

	void Mcf5206e::advanceWatchdog(const uint32_t _cycles)
	{
		if(!(m_sypcr & 0x80))
			return;

		m_watchdogCounter += _cycles;
		if(m_watchdogCounter < watchdogTimeout())
			return;

		m_watchdogCounter = 0;

		if(m_sypcr & 0x40)
			watchdogReset();
		else
			m_ipr |= 1u << SrcSwt;
	}

	// ---------------------------------------------------------------------------------------------------
	// address decoding

	void Mcf5206e::rebuildMemoryMap()
	{
		for(uint32_t p = 0; p < 65536; ++p)
		{
			const uint32_t addr = p << 16;
			Page& page = m_pages[p];
			page = Page();

			auto assign = [&](const Region _region, const uint32_t _base, const uint32_t _offsetMask, const uint8_t _portSize, const uint8_t _wait, const bool _rd, const bool _wr)
			{
				Device* dev = m_devices[static_cast<size_t>(_region)];
				page.device = dev;
				page.base = _base;
				page.portSize = _portSize;
				page.waitCycles = _wait;
				page.read = _rd;
				page.write = _wr;
				page.memoryMask = _offsetMask;
				if(dev && dev->getMemory())
				{
					page.memory = dev->getMemory();
					page.memoryMask = _offsetMask & (dev->getMemorySize() - 1);
				}
			};

			auto portSizeOf = [](const uint32_t _ps) -> uint8_t
			{
				switch(_ps & 3)
				{
				case 0: return 4;
				case 1: return 1;
				default: return 2;
				}
			};

			if(m_globalChipSelect)
			{
				const uint16_t cscr = m_cscr[0];
				assign(Region::ChipSelect0, 0, 0xffffffff, portSizeOf(cscr >> 6), static_cast<uint8_t>(g_busCycleOverhead + ((cscr >> 10) & 0xf)), true, true);
				continue;
			}

			bool matched = false;

			for(uint32_t cs = 0; cs < 8 && !matched; ++cs)
			{
				const uint16_t cscr = m_cscr[cs];
				if(!(cscr & 3))
					continue;
				const uint32_t bam = m_csmr[cs] & 0xffff0000;
				const uint32_t base = (static_cast<uint32_t>(m_csar[cs]) << 16) & ~bam;
				if((addr & ~bam) != base)
					continue;
				assign(static_cast<Region>(cs), base, bam | 0xffff, portSizeOf(cscr >> 6), static_cast<uint8_t>(g_busCycleOverhead + ((cscr >> 10) & 0xf)), (cscr & 1) != 0, (cscr & 2) != 0);
				matched = true;
			}

			for(uint32_t b = 0; b < 2 && !matched; ++b)
			{
				const uint8_t dccr = m_dccr[b];
				if(!(dccr & 3))
					continue;
				const uint32_t bam = m_dcmr[b] & 0xfffe0000;
				const uint32_t base = (static_cast<uint32_t>(m_dcar[b]) << 16) & 0xfffe0000 & ~bam;
				if((addr & 0xfffe0000 & ~bam) != base)
					continue;
				assign(b == 0 ? Region::Dram0 : Region::Dram1, base, bam | 0x1ffff, portSizeOf(dccr >> 6), g_dramWaitCycles, (dccr & 1) != 0, (dccr & 2) != 0);
				matched = true;
			}
		}
	}

	uint32_t Mcf5206e::access(const uint32_t _addr, const uint32_t _value, const uint32_t _size, const bool _write)
	{
		// internal modules
		if((m_mbar & 1) && (_addr & 0xfffffc00) == (m_mbar & 0xfffffc00))
		{
			// the modules catch up first, so the register shows the state at this instruction, and again
			// afterwards, so whatever the access changed counts from now on
			syncPeripherals();
			const uint32_t offset = _addr & 0x3ff;
			uint32_t result = 0;
			if(_write)
			{
				if(m_moduleWriteMonitor)
					m_moduleWriteMonitor(offset, _value, _size);
				writeModule(offset, _value, _size);
			}
			else
			{
				result = readModule(offset, _size);
			}
			syncPeripherals();
			return result;
		}

		// SRAM, UM section 5
		if((m_rambar & 1) && (_addr & ~(SramSize - 1)) == (m_rambar & ~(SramSize - 1)))
		{
			const uint32_t offset = _addr & (SramSize - 1);
			if(offset + _size <= SramSize)
			{
				if(_write)
				{
					if(!(m_rambar & 0x100))	// WP
						writeBE(&m_sram[offset], _value, _size);
					return 0;
				}
				return readBE(&m_sram[offset], _size);
			}
		}

		return accessExternal(_addr, _value, _size, _write);
	}

	uint32_t Mcf5206e::accessExternal(const uint32_t _addr, const uint32_t _value, const uint32_t _size, const bool _write)
	{
		// an access that crosses a 64 KB page is split into bytes
		if(((_addr & 0xffff) + _size) > 0x10000)
		{
			uint32_t v = 0;
			for(uint32_t i = 0; i < _size; ++i)
			{
				const uint32_t shift = (_size - 1 - i) * 8;
				if(_write)
					accessExternal(_addr + i, (_value >> shift) & 0xff, 1, true);
				else
					v = (v << 8) | accessExternal(_addr + i, 0, 1, false);
			}
			return v;
		}

		const Page& page = m_pages[_addr >> 16];

		if(_write ? !page.write : !page.read)
			return _write ? 0 : 0xffffffffu >> ((4 - _size) * 8);

		const uint32_t busCycles = _size > page.portSize ? _size / page.portSize : 1;
		m_waitCycles += page.waitCycles * busCycles;

		const uint32_t offset = _addr & page.memoryMask;

		if(page.memory)
		{
			// plain memory: wrap inside the device like the address lines do
			if(offset + _size <= page.memoryMask + 1u)
			{
				if(_write)
				{
					writeBE(page.memory + offset, _value, _size);
					return 0;
				}
				return readBE(page.memory + offset, _size);
			}
		}

		if(!page.device)
			return _write ? 0 : 0xffffffffu >> ((4 - _size) * 8);

		// split to the port size, most significant part first at the lowest address
		const uint32_t step = _size < page.portSize ? _size : page.portSize;
		uint32_t v = 0;

		for(uint32_t i = 0; i < _size; i += step)
		{
			const uint32_t shift = (_size - i - step) * 8;
			const uint32_t mask = step == 4 ? 0xffffffffu : ((1u << (step * 8)) - 1);
			if(_write)
				page.device->write((_addr + i) & page.memoryMask, (_value >> shift) & mask, step);
			else
				v = (v << (step * 8)) | (page.device->read((_addr + i) & page.memoryMask, step) & mask);
		}
		return v;
	}

	uint16_t Mcf5206e::fetch16(const uint32_t _addr)
	{
		// ponytail: with the instruction cache enabled (CACR CENB) every fetch is treated as a hit
		const uint32_t waitBefore = m_waitCycles;
		const auto v = static_cast<uint16_t>(access(_addr, 0, 2, false));
		if(m_cacr & 0x80000000)
			m_waitCycles = waitBefore;
		return v;
	}

	uint8_t Mcf5206e::peek8(const uint32_t _addr)
	{
		if((m_mbar & 1) && (_addr & 0xfffffc00) == (m_mbar & 0xfffffc00))
			return 0;
		const uint32_t waitBefore = m_waitCycles;
		const auto v = static_cast<uint8_t>(access(_addr, 0, 1, false));
		m_waitCycles = waitBefore;
		return v;
	}

	// ---------------------------------------------------------------------------------------------------
	// module registers, accessed byte by byte so that any access size works

	uint32_t Mcf5206e::readModule(const uint32_t _offset, const uint32_t _size)
	{
		uint32_t v = 0;
		for(uint32_t i = 0; i < _size; ++i)
			v = (v << 8) | readModuleByte(_offset + i);
		return v;
	}

	void Mcf5206e::writeModule(const uint32_t _offset, const uint32_t _value, const uint32_t _size)
	{
		// 16 bit timer registers are written as a whole, a byte write would clear TCN twice etc.
		if(_size == 2 && _offset >= 0x100 && _offset < 0x140 && !(_offset & 1))
		{
			const uint32_t t = (_offset - 0x100) >> 5;
			const uint32_t reg = (_offset - 0x100) & 0x1f;
			if(reg < 0x10)
			{
				m_timers[t].write16(reg, static_cast<uint16_t>(_value));
				return;
			}
		}

		for(uint32_t i = 0; i < _size; ++i)
			writeModuleByte(_offset + i, static_cast<uint8_t>(_value >> ((_size - 1 - i) * 8)));
	}

	uint8_t Mcf5206e::readModuleByte(const uint32_t _offset)
	{
		if(_offset >= 0x064 && _offset < 0x0c4)
		{
			// chip select registers: 12 bytes per bank, CSAR (2), pad (2), CSMR (4), pad (2), CSCR (2)
			const uint32_t cs = (_offset - 0x064) / 12;
			const uint32_t r = (_offset - 0x064) % 12;
			if(r < 2)	return regByte(m_csar[cs], 2, r);
			if(r >= 4 && r < 8)	return regByte(m_csmr[cs], 4, r - 4);
			if(r >= 10)	return regByte(m_cscr[cs], 2, r - 10);
			return 0;
		}

		if(_offset >= 0x100 && _offset < 0x140)
		{
			const uint32_t t = (_offset - 0x100) >> 5;
			return m_timers[t].read8((_offset - 0x100) & 0x1f);
		}

		if(_offset >= 0x140 && _offset < 0x1c0)
		{
			const uint32_t u = (_offset - 0x140) >> 6;
			const uint32_t reg = (_offset - 0x140) & 0x3f;
			if(reg & 3)
				return 0;
			return m_uarts[u].read(reg);
		}

		if(_offset >= 0x1e0 && _offset < 0x200)
			return m_mbus[_offset - 0x1e0];

		if(_offset >= 0x200 && _offset < 0x280)
			return m_dma[_offset - 0x200];

		switch(_offset)
		{
		case 0x003: return m_simr;
		case 0x036: return regByte(m_imr, 2, 0);
		case 0x037: return regByte(m_imr, 2, 1);
		case 0x03a: return regByte(m_ipr, 2, 0);
		case 0x03b: return regByte(m_ipr, 2, 1);
		case 0x040: return m_rsr;
		case 0x041: return m_sypcr;
		case 0x042: return m_swivr;
		case 0x046: return regByte(m_dcrr, 2, 0);
		case 0x047: return regByte(m_dcrr, 2, 1);
		case 0x04a: return regByte(m_dctr, 2, 0);
		case 0x04b: return regByte(m_dctr, 2, 1);
		case 0x04c: return regByte(m_dcar[0], 2, 0);
		case 0x04d: return regByte(m_dcar[0], 2, 1);
		case 0x050: case 0x051: case 0x052: case 0x053: return regByte(m_dcmr[0], 4, _offset - 0x050);
		case 0x057: return m_dccr[0];
		case 0x058: return regByte(m_dcar[1], 2, 0);
		case 0x059: return regByte(m_dcar[1], 2, 1);
		case 0x05c: case 0x05d: case 0x05e: case 0x05f: return regByte(m_dcmr[1], 4, _offset - 0x05c);
		case 0x063: return m_dccr[1];
		case 0x0c6: return regByte(m_dmcr, 2, 0);
		case 0x0c7: return regByte(m_dmcr, 2, 1);
		case 0x0ca: return regByte(m_par, 2, 0);
		case 0x0cb: return regByte(m_par, 2, 1);
		case 0x1c5: return m_ppddr;
		case 0x1c9: return static_cast<uint8_t>((m_ppdat & m_ppddr) | (m_ppInput & ~m_ppddr));
		default:
			if(_offset >= 0x014 && _offset <= 0x022)
				return m_icr[_offset - 0x013];
			return 0;
		}
	}

	void Mcf5206e::writeModuleByte(const uint32_t _offset, const uint8_t _value)
	{
		if(_offset >= 0x064 && _offset < 0x0c4)
		{
			const uint32_t cs = (_offset - 0x064) / 12;
			const uint32_t r = (_offset - 0x064) % 12;
			if(r < 2)
			{
				setRegByte(m_csar[cs], r, _value);
			}
			else if(r >= 4 && r < 8)
			{
				setRegByte(m_csmr[cs], r - 4, _value);
				// UM 9.3.2: the first write to CSMR0 ends the global chip select
				if(cs == 0)
					m_globalChipSelect = false;
			}
			else if(r >= 10)
			{
				setRegByte(m_cscr[cs], r - 10, _value);
			}
			rebuildMemoryMap();
			return;
		}

		if(_offset >= 0x100 && _offset < 0x140)
		{
			const uint32_t t = (_offset - 0x100) >> 5;
			m_timers[t].write8((_offset - 0x100) & 0x1f, _value);
			return;
		}

		if(_offset >= 0x140 && _offset < 0x1c0)
		{
			const uint32_t u = (_offset - 0x140) >> 6;
			const uint32_t reg = (_offset - 0x140) & 0x3f;
			if(!(reg & 3))
				m_uarts[u].write(reg, _value);
			return;
		}

		if(_offset >= 0x1e0 && _offset < 0x200)
		{
			m_mbus[_offset - 0x1e0] = _value;
			return;
		}

		if(_offset >= 0x200 && _offset < 0x280)
		{
			m_dma[_offset - 0x200] = _value;
			return;
		}

		switch(_offset)
		{
		case 0x003: m_simr = _value; break;
		case 0x036: setRegByte(m_imr, 0, _value); break;
		case 0x037: setRegByte(m_imr, 1, _value); break;
		case 0x040: m_rsr &= ~_value; break;
		case 0x041:
			// UM 8.3.2.7: write once after reset
			if(!m_sypcrWritten)
			{
				m_sypcr = _value;
				m_sypcrWritten = true;
				m_watchdogCounter = 0;
			}
			break;
		case 0x042: m_swivr = _value; break;
		case 0x043:
			if(_value == 0xaa && m_swsrLast == 0x55)
				m_watchdogCounter = 0;
			m_swsrLast = _value;
			break;
		case 0x046: setRegByte(m_dcrr, 0, _value); break;
		case 0x047: setRegByte(m_dcrr, 1, _value); break;
		case 0x04a: setRegByte(m_dctr, 0, _value); break;
		case 0x04b: setRegByte(m_dctr, 1, _value); break;
		case 0x04c: setRegByte(m_dcar[0], 0, _value); rebuildMemoryMap(); break;
		case 0x04d: setRegByte(m_dcar[0], 1, _value); rebuildMemoryMap(); break;
		case 0x050: case 0x051: case 0x052: case 0x053: setRegByte(m_dcmr[0], _offset - 0x050, _value); rebuildMemoryMap(); break;
		case 0x057: m_dccr[0] = _value; rebuildMemoryMap(); break;
		case 0x058: setRegByte(m_dcar[1], 0, _value); rebuildMemoryMap(); break;
		case 0x059: setRegByte(m_dcar[1], 1, _value); rebuildMemoryMap(); break;
		case 0x05c: case 0x05d: case 0x05e: case 0x05f: setRegByte(m_dcmr[1], _offset - 0x05c, _value); rebuildMemoryMap(); break;
		case 0x063: m_dccr[1] = _value; rebuildMemoryMap(); break;
		case 0x0c6: setRegByte(m_dmcr, 0, _value); break;
		case 0x0c7: setRegByte(m_dmcr, 1, _value); break;
		case 0x0ca: setRegByte(m_par, 0, _value); break;
		case 0x0cb: setRegByte(m_par, 1, _value); break;
		case 0x1c5: m_ppddr = _value; break;
		case 0x1c9: m_ppdat = _value; break;
		default:
			if(_offset >= 0x014 && _offset <= 0x022)
			{
				const uint32_t i = _offset - 0x013;
				// UM 8.3.2.3: AVEC of the timers and MBUS reads as one, external and SWT levels are fixed
				uint8_t v = _value;
				if(i == SrcTimer1 || i == SrcTimer2 || i == SrcMbus)
					v |= 0x80;
				if(i <= SrcSwt)
					v = static_cast<uint8_t>((v & 0x83) | (m_icr[i] & 0x1c));
				if(i == SrcSwt)
					v &= 0x7f;
				m_icr[i] = v;
			}
			break;
		}
	}
}
