#include "cfCpu.h"

#include <limits>

// References: ColdFire Family Programmer's Reference Manual Rev 1.0 (PRM), MCF5206e User's Manual (UM),
// chapter 3 for the exception model and the instruction timing tables.

namespace coldfire
{
	namespace
	{
		// one bit per effective address mode, to describe which modes an instruction accepts
		enum EaMode : uint32_t
		{
			MDn = 1 << 0, MAn = 1 << 1, MInd = 1 << 2, MPost = 1 << 3, MPre = 1 << 4, MD16 = 1 << 5, MIdx = 1 << 6,
			MAbsW = 1 << 7, MAbsL = 1 << 8, MPcD16 = 1 << 9, MPcIdx = 1 << 10, MImm = 1 << 11,

			EaAll = MDn | MAn | MInd | MPost | MPre | MD16 | MIdx | MAbsW | MAbsL | MPcD16 | MPcIdx | MImm,
			EaData = EaAll & ~MAn,
			EaMemAlt = MInd | MPost | MPre | MD16 | MIdx | MAbsW | MAbsL,
			EaDataAlt = MDn | EaMemAlt,
			EaAlt = MDn | MAn | EaMemAlt,
			EaControl = MInd | MD16 | MIdx | MAbsW | MAbsL | MPcD16 | MPcIdx,
		};

		uint32_t eaModeBit(const uint32_t _mode, const uint32_t _reg)
		{
			if(_mode < 7)
				return 1u << _mode;
			switch(_reg)
			{
			case 0: return MAbsW;
			case 1: return MAbsL;
			case 2: return MPcD16;
			case 3: return MPcIdx;
			case 4: return MImm;
			default: return 0;
			}
		}

		bool eaAllowed(const uint32_t _op, const uint32_t _allowed)
		{
			return (eaModeBit((_op >> 3) & 7, _op & 7) & _allowed) != 0;
		}

		constexpr uint32_t msb(const uint32_t _sizeBytes)
		{
			return 1u << (_sizeBytes * 8 - 1);
		}

		constexpr uint32_t sizeMask(const uint32_t _sizeBytes)
		{
			return _sizeBytes == 4 ? 0xffffffffu : ((1u << (_sizeBytes * 8)) - 1);
		}

		// exception processing time, taken from TRAP (UM table 3-9: 15(1/2)), which is the only exception
		// the timing tables list
		constexpr uint32_t g_exceptionCycles = 15;
	}

	Cpu::Cpu(Bus& _bus) : m_bus(_bus)
	{
		buildTable();
	}

	void Cpu::reset()
	{
		m_sr = SrS | SrIpl;
		m_vbr = 0;
		m_stopped = false;
		m_halted = false;
		m_nmiEdge = false;
		m_inhibitInterrupts = false;
		m_a[7] = m_bus.read32(0);
		m_pc = m_bus.read32(4);
		m_bus.consumeWaitCycles();
	}

	void Cpu::setInterruptLevel(const uint8_t _level)
	{
		if(_level == 7 && m_interruptLevel != 7)
			m_nmiEdge = true;
		m_interruptLevel = _level;
	}

	uint32_t Cpu::step()
	{
		if(m_halted)
			return 1;

		m_cycles = 0;

		if(!m_inhibitInterrupts)
		{
			const uint32_t mask = (m_sr & SrIpl) >> 8;
			const bool take = m_interruptLevel == 7 ? m_nmiEdge : m_interruptLevel > mask;

			if(take)
			{
				if(m_interruptLevel == 7)
					m_nmiEdge = false;
				takeInterrupt(m_interruptLevel);
				m_cycles += m_bus.consumeWaitCycles();
				m_totalCycles += m_cycles;
				return m_cycles;
			}
		}

		if(m_stopped)
		{
			m_totalCycles += 1;
			return 1;
		}

		m_inhibitInterrupts = false;
		m_instructionPc = m_pc;
		const bool trace = (m_sr & SrT) != 0;

		if(m_instructionHook)
			m_instructionHook(m_pc);

		try
		{
			const uint16_t op = fetch16();
			(this->*m_table[op])(op);
			++m_instructionCount;

			if(trace && !m_inhibitInterrupts)
				exception(VecTrace, m_pc);
		}
		catch(const Fault& _fault)
		{
			exception(_fault.vector, _fault.faultPc ? m_instructionPc : m_pc);
		}

		m_cycles += m_bus.consumeWaitCycles();
		m_totalCycles += m_cycles;
		return m_cycles;
	}

	// ---------------------------------------------------------------------------------------------------
	// memory

	uint16_t Cpu::fetch16()
	{
		const uint16_t w = m_bus.fetch16(m_pc);
		m_pc += 2;
		return w;
	}

	uint32_t Cpu::fetch32()
	{
		const uint32_t hi = fetch16();
		return (hi << 16) | fetch16();
	}

	uint32_t Cpu::read(const uint32_t _addr, const Size _size)
	{
		// misaligned operands are split into aligned accesses, UM table 3-4
		switch(_size)
		{
		case Size::Byte:
			return m_bus.read8(_addr);
		case Size::Word:
			if(_addr & 1)
				m_cycles += 2;
			return m_bus.read16(_addr);
		case Size::Long:
		default:
			if(_addr & 1)
				m_cycles += 3;
			else if(_addr & 2)
				m_cycles += 2;
			return m_bus.read32(_addr);
		}
	}

	void Cpu::write(const uint32_t _addr, const uint32_t _value, const Size _size)
	{
		switch(_size)
		{
		case Size::Byte:
			m_bus.write8(_addr, static_cast<uint8_t>(_value));
			break;
		case Size::Word:
			if(_addr & 1)
				m_cycles += 1;
			m_bus.write16(_addr, static_cast<uint16_t>(_value));
			break;
		case Size::Long:
			if(_addr & 1)
				m_cycles += 2;
			else if(_addr & 2)
				m_cycles += 1;
			m_bus.write32(_addr, _value);
			break;
		}
	}

	void Cpu::push32(const uint32_t _value)
	{
		m_a[7] -= 4;
		write(m_a[7], _value, Size::Long);
	}

	uint32_t Cpu::pop32()
	{
		const uint32_t v = read(m_a[7], Size::Long);
		m_a[7] += 4;
		return v;
	}

	// ---------------------------------------------------------------------------------------------------
	// effective addresses

	uint32_t Cpu::indexedAddress(const uint32_t _base)
	{
		const uint32_t ext = fetch16();

		// PRM: only the brief format with a long index and a scale of 1, 2 or 4 exists, everything else
		// is an address error (UM 3.5.2)
		if((ext & 0x0100) || !(ext & 0x0800) || ((ext >> 9) & 3) == 3)
			throw Fault{VecAddressError, true};

		const uint32_t idxReg = (ext >> 12) & 7;
		const uint32_t idx = (ext & 0x8000) ? m_a[idxReg] : m_d[idxReg];
		const uint32_t scale = (ext >> 9) & 3;
		const auto disp = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(ext & 0xff)));

		return _base + disp + (idx << scale);
	}

	Cpu::Ea Cpu::decodeEa(const uint32_t _mode, const uint32_t _reg, const Size _size)
	{
		const auto size = static_cast<uint32_t>(_size);

		switch(_mode)
		{
		case 0:
			return {Ea::Kind::DataReg, _reg};
		case 1:
			return {Ea::Kind::AddrReg, _reg};
		case 2:
			return {Ea::Kind::Memory, m_a[_reg]};
		case 3:
			{
				const uint32_t addr = m_a[_reg];
				m_a[_reg] += size;
				return {Ea::Kind::Memory, addr};
			}
		case 4:
			m_a[_reg] -= size;
			return {Ea::Kind::Memory, m_a[_reg]};
		case 5:
			{
				const auto d16 = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(fetch16())));
				return {Ea::Kind::Memory, m_a[_reg] + d16};
			}
		case 6:
			return {Ea::Kind::Memory, indexedAddress(m_a[_reg])};
		default:
			switch(_reg)
			{
			case 0:
				return {Ea::Kind::Memory, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(fetch16())))};
			case 1:
				return {Ea::Kind::Memory, fetch32()};
			case 2:
				{
					const uint32_t base = m_pc;
					const auto d16 = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(fetch16())));
					return {Ea::Kind::Memory, base + d16};
				}
			case 3:
				{
					const uint32_t base = m_pc;
					return {Ea::Kind::Memory, indexedAddress(base)};
				}
			case 4:
				switch(_size)
				{
				case Size::Byte:	return {Ea::Kind::Immediate, fetch16() & 0xffu};
				case Size::Word:	return {Ea::Kind::Immediate, fetch16()};
				default:			return {Ea::Kind::Immediate, fetch32()};
				}
			default:
				throw Fault{VecIllegalInstruction, true};
			}
		}
	}

	uint32_t Cpu::readEa(const Ea& _ea, const Size _size)
	{
		switch(_ea.kind)
		{
		case Ea::Kind::DataReg:		return m_d[_ea.value] & sizeMask(static_cast<uint32_t>(_size));
		case Ea::Kind::AddrReg:		return m_a[_ea.value] & sizeMask(static_cast<uint32_t>(_size));
		case Ea::Kind::Memory:		return read(_ea.value, _size);
		case Ea::Kind::Immediate:
		default:					return _ea.value;
		}
	}

	void Cpu::writeEa(const Ea& _ea, const uint32_t _value, const Size _size)
	{
		switch(_ea.kind)
		{
		case Ea::Kind::DataReg:
			{
				const uint32_t mask = sizeMask(static_cast<uint32_t>(_size));
				m_d[_ea.value] = (m_d[_ea.value] & ~mask) | (_value & mask);
			}
			break;
		case Ea::Kind::AddrReg:
			m_a[_ea.value] = _value;
			break;
		case Ea::Kind::Memory:
			write(_ea.value, _value, _size);
			break;
		case Ea::Kind::Immediate:
			throw Fault{VecIllegalInstruction, true};
		}
	}

	uint32_t Cpu::controlAddress(const uint32_t _mode, const uint32_t _reg)
	{
		// only called for control modes, which never auto-increment
		return decodeEa(_mode, _reg, Size::Long).value;
	}

	uint32_t Cpu::eaClass(const uint32_t _mode, const uint32_t _reg)
	{
		if(_mode <= 1)
			return 0;
		if(_mode == 6)
			return 2;
		if(_mode == 7)
		{
			if(_reg == 3)
				return 2;
			if(_reg == 4)
				return 0;
		}
		return 1;
	}

	// ---------------------------------------------------------------------------------------------------
	// exceptions

	void Cpu::exception(const uint8_t _vector, const uint32_t _stackedPc, const uint8_t _faultStatus)
	{
		const uint16_t oldSr = m_sr;

		m_sr = static_cast<uint16_t>((m_sr | SrS) & ~SrT);
		m_stopped = false;

		// UM 3.4 / table 3-2: the frame is written at a 0-mod-4 address, the format field records the
		// misalignment of the original A7
		const uint32_t sp = m_a[7];
		const uint32_t format = 4 + (sp & 3);
		const uint32_t frame = (sp & ~3u) - 8;

		const uint32_t fv = (format << 12) | ((_faultStatus & 0xcu) << 8) | (static_cast<uint32_t>(_vector) << 2) | (_faultStatus & 3u);

		m_bus.write32(frame + 4, _stackedPc);
		m_bus.write32(frame, (fv << 16) | oldSr);
		m_a[7] = frame;

		const uint32_t handler = m_bus.read32(m_vbr + static_cast<uint32_t>(_vector) * 4);

		m_cycles += g_exceptionCycles;
		m_inhibitInterrupts = true;

		// UM 3.5.10: a fault while processing an exception halts the core
		if(handler & 1)
		{
			m_halted = true;
			return;
		}

		m_pc = handler;
	}

	void Cpu::takeInterrupt(const uint8_t _level)
	{
		const uint8_t vector = m_bus.interruptAcknowledge(_level);

		// UM 3.3: the frame holds the SR from before the interrupt, which then forces M to zero and the
		// mask to the level of the request
		exception(vector, m_pc);
		m_sr = static_cast<uint16_t>((m_sr & ~(SrIpl | SrM)) | (static_cast<uint32_t>(_level) << 8));
	}

	void Cpu::requireSupervisor() const
	{
		if(!(m_sr & SrS))
			throw Fault{VecPrivilegeViolation, true};
	}

	void Cpu::setSR16(const uint16_t _sr)
	{
		m_sr = _sr & SrMask;
	}

	void Cpu::jump(const uint32_t _target)
	{
		// UM 3.5.2: transferring control to an odd address is an address error
		if(_target & 1)
			throw Fault{VecAddressError, true};
		m_pc = _target;
	}

	// ---------------------------------------------------------------------------------------------------
	// condition codes

	bool Cpu::testCondition(const uint32_t _cc) const
	{
		const bool c = (m_sr & SrC) != 0;
		const bool v = (m_sr & SrV) != 0;
		const bool z = (m_sr & SrZ) != 0;
		const bool n = (m_sr & SrN) != 0;

		switch(_cc & 0xf)
		{
		case 0x0: return true;
		case 0x1: return false;
		case 0x2: return !c && !z;
		case 0x3: return c || z;
		case 0x4: return !c;
		case 0x5: return c;
		case 0x6: return !z;
		case 0x7: return z;
		case 0x8: return !v;
		case 0x9: return v;
		case 0xa: return !n;
		case 0xb: return n;
		case 0xc: return n == v;
		case 0xd: return n != v;
		case 0xe: return !z && n == v;
		case 0xf:
		default:  return z || n != v;
		}
	}

	void Cpu::setNZ(const uint32_t _result, const Size _size)
	{
		const auto size = static_cast<uint32_t>(_size);
		m_sr &= ~(SrN | SrZ);
		if(_result & msb(size))
			m_sr |= SrN;
		if(!(_result & sizeMask(size)))
			m_sr |= SrZ;
	}

	void Cpu::setLogicFlags(const uint32_t _result, const Size _size)
	{
		m_sr &= ~(SrV | SrC);
		setNZ(_result, _size);
	}

	uint32_t Cpu::add(const uint32_t _src, const uint32_t _dst, const bool _withX)
	{
		const uint32_t x = (_withX && (m_sr & SrX)) ? 1 : 0;
		const uint64_t wide = static_cast<uint64_t>(_src) + _dst + x;
		const auto r = static_cast<uint32_t>(wide);

		uint16_t f = 0;
		if(wide >> 32)
			f |= SrC | SrX;
		if((~(_src ^ _dst) & (_src ^ r)) & 0x80000000)
			f |= SrV;
		if(r & 0x80000000)
			f |= SrN;

		// ADDX only clears Z, so that a chain of them tests the whole multi precision result
		if(_withX)
		{
			if(r || !(m_sr & SrZ))
				m_sr &= ~SrZ;
			m_sr = static_cast<uint16_t>((m_sr & ~(SrX | SrN | SrV | SrC)) | f);
		}
		else
		{
			if(!r)
				f |= SrZ;
			m_sr = static_cast<uint16_t>((m_sr & ~(SrX | SrN | SrZ | SrV | SrC)) | f);
		}
		return r;
	}

	uint32_t Cpu::sub(const uint32_t _src, const uint32_t _dst, const bool _withX)
	{
		const uint32_t x = (_withX && (m_sr & SrX)) ? 1 : 0;
		const uint64_t wide = static_cast<uint64_t>(_dst) - _src - x;
		const auto r = static_cast<uint32_t>(wide);

		uint16_t f = 0;
		if(wide >> 32)
			f |= SrC | SrX;
		if(((_src ^ _dst) & (_dst ^ r)) & 0x80000000)
			f |= SrV;
		if(r & 0x80000000)
			f |= SrN;

		if(_withX)
		{
			if(r)
				m_sr &= ~SrZ;
			m_sr = static_cast<uint16_t>((m_sr & ~(SrX | SrN | SrV | SrC)) | f);
		}
		else
		{
			if(!r)
				f |= SrZ;
			m_sr = static_cast<uint16_t>((m_sr & ~(SrX | SrN | SrZ | SrV | SrC)) | f);
		}
		return r;
	}

	void Cpu::cmp(const uint32_t _src, const uint32_t _dst)
	{
		const uint64_t wide = static_cast<uint64_t>(_dst) - _src;
		const auto r = static_cast<uint32_t>(wide);

		uint16_t f = 0;
		if(wide >> 32)
			f |= SrC;
		if(((_src ^ _dst) & (_dst ^ r)) & 0x80000000)
			f |= SrV;
		if(r & 0x80000000)
			f |= SrN;
		if(!r)
			f |= SrZ;

		m_sr = static_cast<uint16_t>((m_sr & ~(SrN | SrZ | SrV | SrC)) | f);
	}

	// ---------------------------------------------------------------------------------------------------
	// decode table

	void Cpu::buildTable()
	{
		m_table.fill(&Cpu::opIllegal);

		for(uint32_t op = 0; op < 0x10000; ++op)
		{
			const uint32_t line = op >> 12;
			const uint32_t mode = (op >> 3) & 7;
			const uint32_t opmode = (op >> 6) & 7;

			Handler h = nullptr;

			switch(line)
			{
			case 0x0:
				switch(op & 0xfff8)
				{
				case 0x0080: h = &Cpu::opOriL; break;
				case 0x0280: h = &Cpu::opAndiL; break;
				case 0x0480: h = &Cpu::opSubiL; break;
				case 0x0680: h = &Cpu::opAddiL; break;
				case 0x0a80: h = &Cpu::opEoriL; break;
				case 0x0c80: h = &Cpu::opCmpiL; break;
				default:
					if((op & 0xff00) == 0x0800 && eaAllowed(op, MDn | MInd | MPost | MPre | MD16 | MIdx | MAbsW | MAbsL | ((op & 0xc0) == 0 ? (MPcD16 | MPcIdx) : 0)))
						h = &Cpu::opBitImm;
					else if((op & 0x0100) && eaAllowed(op, (op & 0xc0) == 0 ? EaData : EaDataAlt))
						h = &Cpu::opBitReg;
					break;
				}
				break;
			case 0x1:
			case 0x2:
			case 0x3:
				if(!eaAllowed(op, EaAll))
					break;
				if(opmode == 1)
				{
					if(line != 0x1)
						h = &Cpu::opMovea;
				}
				else if(eaModeBit(opmode, (op >> 9) & 7) & EaDataAlt)
				{
					h = &Cpu::opMove;
				}
				break;
			case 0x4:
				if((op & 0xfff8) == 0x4080) h = &Cpu::opNegxL;
				else if((op & 0xfff8) == 0x40c0) h = &Cpu::opMoveFromSr;
				else if((op & 0xf1c0) == 0x41c0 && eaAllowed(op, EaControl)) h = &Cpu::opLea;
				else if((op & 0xff00) == 0x4200 && (op & 0xc0) != 0xc0 && eaAllowed(op, EaDataAlt)) h = &Cpu::opClr;
				else if((op & 0xfff8) == 0x42c0) h = &Cpu::opMoveFromCcr;
				else if((op & 0xfff8) == 0x4480) h = &Cpu::opNegL;
				else if((op & 0xffc0) == 0x44c0 && eaAllowed(op, MDn | MImm)) h = &Cpu::opMoveToCcr;
				else if((op & 0xfff8) == 0x4680) h = &Cpu::opNotL;
				else if((op & 0xffc0) == 0x46c0 && eaAllowed(op, MDn | MImm)) h = &Cpu::opMoveToSr;
				else if((op & 0xfff8) == 0x4840) h = &Cpu::opSwap;
				else if((op & 0xffc0) == 0x4840 && eaAllowed(op, EaControl)) h = &Cpu::opPea;
				else if((op & 0xfff8) == 0x4880 || (op & 0xfff8) == 0x48c0 || (op & 0xfff8) == 0x49c0) h = &Cpu::opExt;
				else if((op & 0xffc0) == 0x48c0 && eaAllowed(op, MInd | MD16)) h = &Cpu::opMovemToMem;
				else if((op & 0xffc0) == 0x4cc0 && eaAllowed(op, MInd | MD16)) h = &Cpu::opMovemFromMem;
				else if(op == 0x4ac8) h = &Cpu::opHalt;
				else if(op == 0x4acc) h = &Cpu::opPulse;
				else if(op == 0x4afc) h = &Cpu::opIllegal;
				else if((op & 0xff00) == 0x4a00 && (op & 0xc0) != 0xc0 && eaAllowed(op, EaAll)) h = &Cpu::opTst;
				else if((op & 0xffc0) == 0x4c00 && eaAllowed(op, MDn | MInd | MPost | MPre | MD16)) h = &Cpu::opMulL;
				else if((op & 0xffc0) == 0x4c40 && eaAllowed(op, MDn | MInd | MPost | MPre | MD16)) h = &Cpu::opDivL;
				else if((op & 0xfff0) == 0x4e40) h = &Cpu::opTrap;
				else if((op & 0xfff8) == 0x4e50) h = &Cpu::opLink;
				else if((op & 0xfff8) == 0x4e58) h = &Cpu::opUnlk;
				else if(op == 0x4e71) h = &Cpu::opNop;
				else if(op == 0x4e72) h = &Cpu::opStop;
				else if(op == 0x4e73) h = &Cpu::opRte;
				else if(op == 0x4e75) h = &Cpu::opRts;
				else if(op == 0x4e7b) h = &Cpu::opMovec;
				else if((op & 0xffc0) == 0x4e80 && eaAllowed(op, EaControl)) h = &Cpu::opJsr;
				else if((op & 0xffc0) == 0x4ec0 && eaAllowed(op, EaControl)) h = &Cpu::opJmp;
				break;
			case 0x5:
				if(op == 0x51fa || op == 0x51fb || op == 0x51fc) h = &Cpu::opTrapf;
				else if((op & 0xc0) == 0x80 && eaAllowed(op, EaAlt)) h = &Cpu::opAddqSubq;
				else if((op & 0xf8) == 0xc0) h = &Cpu::opScc;
				break;
			case 0x6:
				h = &Cpu::opBcc;
				break;
			case 0x7:
				if(!(op & 0x100))
					h = &Cpu::opMoveq;
				break;
			case 0x8:
				if(opmode == 2 && eaAllowed(op, EaData)) h = &Cpu::opOr;
				else if(opmode == 6 && eaAllowed(op, EaMemAlt)) h = &Cpu::opOr;
				else if((opmode == 3 || opmode == 7) && eaAllowed(op, EaData)) h = &Cpu::opDivW;
				break;
			case 0x9:
				if(opmode == 2 && eaAllowed(op, EaAll)) h = &Cpu::opSub;
				else if(opmode == 6 && mode == 0) h = &Cpu::opSubx;
				else if(opmode == 6 && eaAllowed(op, EaMemAlt)) h = &Cpu::opSub;
				else if(opmode == 7 && eaAllowed(op, EaAll)) h = &Cpu::opSuba;
				break;
			case 0xa:
				h = &Cpu::opLineA;
				break;
			case 0xb:
				if(opmode == 2 && eaAllowed(op, EaAll)) h = &Cpu::opCmp;
				else if(opmode == 7 && eaAllowed(op, EaAll)) h = &Cpu::opCmpa;
				else if(opmode == 6 && eaAllowed(op, EaDataAlt)) h = &Cpu::opEor;
				break;
			case 0xc:
				if(opmode == 2 && eaAllowed(op, EaData)) h = &Cpu::opAnd;
				else if(opmode == 6 && eaAllowed(op, EaMemAlt)) h = &Cpu::opAnd;
				else if((opmode == 3 || opmode == 7) && eaAllowed(op, EaData)) h = &Cpu::opMulW;
				break;
			case 0xd:
				if(opmode == 2 && eaAllowed(op, EaAll)) h = &Cpu::opAdd;
				else if(opmode == 6 && mode == 0) h = &Cpu::opAddx;
				else if(opmode == 6 && eaAllowed(op, EaMemAlt)) h = &Cpu::opAdd;
				else if(opmode == 7 && eaAllowed(op, EaAll)) h = &Cpu::opAdda;
				break;
			case 0xe:
				// only long register shifts, ASx and LSx
				if((op & 0xc0) == 0x80 && (op & 0x10) == 0)
					h = &Cpu::opShift;
				break;
			case 0xf:
				if((op & 0xff20) == 0xf420 && (op & 0x18) == 0x08) h = &Cpu::opCpushl;
				else if((op & 0xff00) == 0xfb00 && (op & 0xc0) != 0xc0 && eaAllowed(op, EaMemAlt | MPcD16 | MPcIdx | MImm | MDn)) h = &Cpu::opWddata;
				else if((op & 0xffc0) == 0xfbc0 && eaAllowed(op, MInd | MD16)) h = &Cpu::opWdebug;
				else h = &Cpu::opLineF;
				break;
			default:
				break;
			}

			if(h)
				m_table[op] = h;
		}
	}

	// ---------------------------------------------------------------------------------------------------
	// line 0: immediate and bit operations

	void Cpu::opIllegal(const uint16_t)
	{
		throw Fault{VecIllegalInstruction, true};
	}

	void Cpu::opLineA(const uint16_t _op)
	{
		// MAC opcodes live here; the SDK code does not use them, report instead of guessing
		if(m_unimplemented)
			m_unimplemented(m_instructionPc, _op);
		throw Fault{VecLineA, true};
	}

	void Cpu::opLineF(const uint16_t)
	{
		throw Fault{VecLineF, true};
	}

	void Cpu::opOriL(const uint16_t _op)
	{
		const uint32_t r = m_d[_op & 7] | fetch32();
		m_d[_op & 7] = r;
		setLogicFlags(r, Size::Long);
		m_cycles += 1;
	}

	void Cpu::opAndiL(const uint16_t _op)
	{
		const uint32_t r = m_d[_op & 7] & fetch32();
		m_d[_op & 7] = r;
		setLogicFlags(r, Size::Long);
		m_cycles += 1;
	}

	void Cpu::opSubiL(const uint16_t _op)
	{
		const uint32_t imm = fetch32();
		m_d[_op & 7] = sub(imm, m_d[_op & 7], false);
		m_cycles += 1;
	}

	void Cpu::opAddiL(const uint16_t _op)
	{
		const uint32_t imm = fetch32();
		m_d[_op & 7] = add(imm, m_d[_op & 7], false);
		m_cycles += 1;
	}

	void Cpu::opEoriL(const uint16_t _op)
	{
		const uint32_t r = m_d[_op & 7] ^ fetch32();
		m_d[_op & 7] = r;
		setLogicFlags(r, Size::Long);
		m_cycles += 1;
	}

	void Cpu::opCmpiL(const uint16_t _op)
	{
		const uint32_t imm = fetch32();
		cmp(imm, m_d[_op & 7]);
		m_cycles += 1;
	}

	namespace
	{
		// 0 BTST, 1 BCHG, 2 BCLR, 3 BSET
		uint32_t applyBitOp(const uint32_t _type, const uint32_t _value, const uint32_t _mask)
		{
			switch(_type)
			{
			case 1:		return _value ^ _mask;
			case 2:		return _value & ~_mask;
			case 3:		return _value | _mask;
			default:	return _value;
			}
		}
	}

	void Cpu::opBitImm(const uint16_t _op)
	{
		const uint32_t type = (_op >> 6) & 3;
		const uint32_t bit = fetch16() & 0xff;
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;

		if(mode == 0)
		{
			const uint32_t mask = 1u << (bit & 31);
			if(m_d[reg] & mask) m_sr &= ~SrZ; else m_sr |= SrZ;
			m_d[reg] = applyBitOp(type, m_d[reg], mask);
			m_cycles += type == 0 ? 1 : 2;
			return;
		}

		const Ea ea = decodeEa(mode, reg, Size::Byte);
		const uint32_t mask = 1u << (bit & 7);
		const uint32_t v = readEa(ea, Size::Byte);
		if(v & mask) m_sr &= ~SrZ; else m_sr |= SrZ;
		if(type)
			writeEa(ea, applyBitOp(type, v, mask), Size::Byte);

		const uint32_t cls = eaClass(mode, reg);
		m_cycles += (type == 0 ? 3 : 4) + (cls == 2 ? 1 : 0);
	}

	void Cpu::opBitReg(const uint16_t _op)
	{
		const uint32_t type = (_op >> 6) & 3;
		const uint32_t bit = m_d[(_op >> 9) & 7];
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;

		if(mode == 0)
		{
			const uint32_t mask = 1u << (bit & 31);
			if(m_d[reg] & mask) m_sr &= ~SrZ; else m_sr |= SrZ;
			m_d[reg] = applyBitOp(type, m_d[reg], mask);
			m_cycles += 2;
			return;
		}

		const Ea ea = decodeEa(mode, reg, Size::Byte);
		const uint32_t mask = 1u << (bit & 7);
		const uint32_t v = readEa(ea, Size::Byte);
		if(v & mask) m_sr &= ~SrZ; else m_sr |= SrZ;
		if(type)
			writeEa(ea, applyBitOp(type, v, mask), Size::Byte);

		const uint32_t cls = eaClass(mode, reg);
		m_cycles += (type == 0 ? 3 : 4) + (cls == 2 ? 1 : 0);
	}

	// ---------------------------------------------------------------------------------------------------
	// lines 1-3: MOVE, MOVEA

	void Cpu::opMove(const uint16_t _op)
	{
		const uint32_t line = _op >> 12;
		const Size size = line == 1 ? Size::Byte : (line == 3 ? Size::Word : Size::Long);

		const uint32_t srcMode = (_op >> 3) & 7;
		const uint32_t srcReg = _op & 7;
		const uint32_t dstMode = (_op >> 6) & 7;
		const uint32_t dstReg = (_op >> 9) & 7;

		const Ea src = decodeEa(srcMode, srcReg, size);
		const uint32_t v = readEa(src, size);
		const Ea dst = decodeEa(dstMode, dstReg, size);
		writeEa(dst, v, size);
		setLogicFlags(v, size);

		// UM tables 3-5 and 3-6
		const uint32_t srcClass = eaClass(srcMode, srcReg);
		const uint32_t dstClass = eaClass(dstMode, dstReg);
		const bool srcImm = srcMode == 7 && srcReg == 4;

		uint32_t c;
		if(srcImm)
			c = dstClass == 0 ? 1 : (size == Size::Long ? 2 : 3);
		else if(srcClass == 0)
			c = dstClass == 2 ? 2 : 1;
		else
			c = (size == Size::Long ? 2 : 3) + (srcClass == 2 || dstClass == 2 ? 1 : 0);
		m_cycles += c;
	}

	void Cpu::opMovea(const uint16_t _op)
	{
		const bool word = (_op >> 12) == 3;
		const uint32_t srcMode = (_op >> 3) & 7;
		const uint32_t srcReg = _op & 7;

		const Ea src = decodeEa(srcMode, srcReg, word ? Size::Word : Size::Long);
		uint32_t v = readEa(src, word ? Size::Word : Size::Long);
		if(word)
			v = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(v)));
		m_a[(_op >> 9) & 7] = v;

		const uint32_t srcClass = eaClass(srcMode, srcReg);
		m_cycles += srcClass == 0 ? 1 : (word ? 3 : 2) + (srcClass == 2 ? 1 : 0);
	}

	// ---------------------------------------------------------------------------------------------------
	// line 4: miscellaneous

	void Cpu::opNegxL(const uint16_t _op)
	{
		m_d[_op & 7] = sub(m_d[_op & 7], 0, true);
		m_cycles += 1;
	}

	void Cpu::opMoveFromSr(const uint16_t _op)
	{
		requireSupervisor();
		m_d[_op & 7] = (m_d[_op & 7] & 0xffff0000) | m_sr;
		m_cycles += 1;
	}

	void Cpu::opLea(const uint16_t _op)
	{
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;
		m_a[(_op >> 9) & 7] = controlAddress(mode, reg);
		m_cycles += eaClass(mode, reg) == 2 ? 2 : 1;
	}

	void Cpu::opClr(const uint16_t _op)
	{
		const uint32_t sz = (_op >> 6) & 3;
		const Size size = sz == 0 ? Size::Byte : (sz == 1 ? Size::Word : Size::Long);
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;

		const Ea ea = decodeEa(mode, reg, size);
		writeEa(ea, 0, size);
		m_sr = static_cast<uint16_t>((m_sr & ~(SrN | SrV | SrC)) | SrZ);
		m_cycles += eaClass(mode, reg) == 2 ? 2 : 1;
	}

	void Cpu::opMoveFromCcr(const uint16_t _op)
	{
		m_d[_op & 7] = (m_d[_op & 7] & 0xffff0000) | (m_sr & 0x1f);
		m_cycles += 1;
	}

	void Cpu::opNegL(const uint16_t _op)
	{
		m_d[_op & 7] = sub(m_d[_op & 7], 0, false);
		m_cycles += 1;
	}

	void Cpu::opMoveToCcr(const uint16_t _op)
	{
		const Ea ea = decodeEa((_op >> 3) & 7, _op & 7, Size::Word);
		const uint32_t v = readEa(ea, Size::Word);
		m_sr = static_cast<uint16_t>((m_sr & 0xff00) | (v & 0x1f));
		m_cycles += 1;
	}

	void Cpu::opNotL(const uint16_t _op)
	{
		const uint32_t r = ~m_d[_op & 7];
		m_d[_op & 7] = r;
		setLogicFlags(r, Size::Long);
		m_cycles += 1;
	}

	void Cpu::opMoveToSr(const uint16_t _op)
	{
		requireSupervisor();
		const Ea ea = decodeEa((_op >> 3) & 7, _op & 7, Size::Word);
		const auto v = static_cast<uint16_t>(readEa(ea, Size::Word));
		setSR16(v);

		// UM table 3-9, note 2
		m_cycles += (ea.kind == Ea::Kind::Immediate && (v & SrS)) ? 1 : 7;
	}

	void Cpu::opSwap(const uint16_t _op)
	{
		const uint32_t v = m_d[_op & 7];
		const uint32_t r = (v << 16) | (v >> 16);
		m_d[_op & 7] = r;
		setLogicFlags(r, Size::Long);
		m_cycles += 1;
	}

	void Cpu::opPea(const uint16_t _op)
	{
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;
		const uint32_t addr = controlAddress(mode, reg);
		push32(addr);
		m_cycles += eaClass(mode, reg) == 2 ? 3 : 2;
	}

	void Cpu::opExt(const uint16_t _op)
	{
		const uint32_t reg = _op & 7;
		const uint32_t v = m_d[reg];

		switch(_op & 0xfff8)
		{
		case 0x4880:	// EXT.W
			{
				const auto w = static_cast<uint32_t>(static_cast<uint16_t>(static_cast<int16_t>(static_cast<int8_t>(v))));
				m_d[reg] = (v & 0xffff0000) | w;
				setLogicFlags(w, Size::Word);
			}
			break;
		case 0x48c0:	// EXT.L
			m_d[reg] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(v)));
			setLogicFlags(m_d[reg], Size::Long);
			break;
		default:		// EXTB.L
			m_d[reg] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(v)));
			setLogicFlags(m_d[reg], Size::Long);
			break;
		}
		m_cycles += 1;
	}

	void Cpu::opMovemToMem(const uint16_t _op)
	{
		const uint32_t mask = fetch16();
		uint32_t addr = controlAddress((_op >> 3) & 7, _op & 7);

		uint32_t count = 0;
		for(uint32_t i = 0; i < 16; ++i)
		{
			if(!(mask & (1u << i)))
				continue;
			write(addr, i < 8 ? m_d[i] : m_a[i - 8], Size::Long);
			addr += 4;
			++count;
		}
		m_cycles += 1 + count;
	}

	void Cpu::opMovemFromMem(const uint16_t _op)
	{
		const uint32_t mask = fetch16();
		uint32_t addr = controlAddress((_op >> 3) & 7, _op & 7);

		uint32_t count = 0;
		for(uint32_t i = 0; i < 16; ++i)
		{
			if(!(mask & (1u << i)))
				continue;
			const uint32_t v = read(addr, Size::Long);
			if(i < 8)
				m_d[i] = v;
			else
				m_a[i - 8] = v;
			addr += 4;
			++count;
		}
		m_cycles += 1 + count;
	}

	void Cpu::opTst(const uint16_t _op)
	{
		const uint32_t sz = (_op >> 6) & 3;
		const Size size = sz == 0 ? Size::Byte : (sz == 1 ? Size::Word : Size::Long);
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;

		const Ea ea = decodeEa(mode, reg, size);
		setLogicFlags(readEa(ea, size), size);

		const uint32_t cls = eaClass(mode, reg);
		m_cycles += cls == 0 ? 1 : (size == Size::Long ? 2 : 3) + (cls == 2 ? 1 : 0);
	}

	void Cpu::opHalt(const uint16_t)
	{
		requireSupervisor();
		m_halted = true;
		m_cycles += 1;
	}

	void Cpu::opPulse(const uint16_t)
	{
		m_cycles += 1;
	}

	void Cpu::opMulL(const uint16_t _op)
	{
		const uint32_t ext = fetch16();
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;

		const Ea ea = decodeEa(mode, reg, Size::Long);
		const uint32_t src = readEa(ea, Size::Long);
		const uint32_t dl = (ext >> 12) & 7;

		// the low 32 bits of the product are the same for signed and unsigned operands
		const uint32_t r = src * m_d[dl];
		m_d[dl] = r;
		setLogicFlags(r, Size::Long);

		m_cycles += eaClass(mode, reg) == 0 ? 18 : 20;
	}

	void Cpu::opDivL(const uint16_t _op)
	{
		const uint32_t ext = fetch16();
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;

		const Ea ea = decodeEa(mode, reg, Size::Long);
		const uint32_t divisor = readEa(ea, Size::Long);

		const uint32_t dx = (ext >> 12) & 7;
		const uint32_t dw = ext & 7;
		const bool isSigned = (ext & 0x0800) != 0;

		m_cycles += 35;

		if(!divisor)
			throw Fault{VecDivideByZero, true};

		const uint32_t dividend = m_d[dx];

		uint32_t quotient;
		uint32_t remainder;

		if(isSigned)
		{
			const auto a = static_cast<int32_t>(dividend);
			const auto b = static_cast<int32_t>(divisor);

			if(a == std::numeric_limits<int32_t>::min() && b == -1)
			{
				// PRM: overflow sets V and leaves the destination unaffected
				m_sr = static_cast<uint16_t>((m_sr & ~SrC) | SrV);
				return;
			}
			quotient = static_cast<uint32_t>(a / b);
			remainder = static_cast<uint32_t>(a % b);
		}
		else
		{
			quotient = dividend / divisor;
			remainder = dividend % divisor;
		}

		// PRM: equal register fields are DIVx.L, different ones REMx.L, which keeps Dx. N and Z describe
		// the quotient for both
		if(dw == dx)
			m_d[dx] = quotient;
		else
			m_d[dw] = remainder;

		setLogicFlags(quotient, Size::Long);
	}

	void Cpu::opTrap(const uint16_t _op)
	{
		exception(static_cast<uint8_t>(VecTrap0 + (_op & 0xf)), m_pc);
	}

	void Cpu::opLink(const uint16_t _op)
	{
		const uint32_t reg = _op & 7;
		const auto d16 = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(fetch16())));

		m_a[7] -= 4;
		write(m_a[7], m_a[reg], Size::Long);
		m_a[reg] = m_a[7];
		m_a[7] += d16;
		m_cycles += 2;
	}

	void Cpu::opUnlk(const uint16_t _op)
	{
		const uint32_t reg = _op & 7;
		m_a[7] = m_a[reg];
		const uint32_t v = read(m_a[7], Size::Long);
		m_a[7] += 4;
		m_a[reg] = v;
		m_cycles += 2;
	}

	void Cpu::opNop(const uint16_t)
	{
		m_cycles += 3;
	}

	void Cpu::opStop(const uint16_t)
	{
		requireSupervisor();
		const uint16_t sr = fetch16();
		setSR16(sr);
		m_stopped = true;
		m_cycles += 3;
	}

	void Cpu::opRte(const uint16_t)
	{
		requireSupervisor();

		const uint32_t sp = m_a[7];
		const uint32_t fvsr = read(sp, Size::Long);
		const uint32_t format = fvsr >> 28;

		// UM 3.5.7: a frame that is not format 4-7 raises a format error and stays on the stack
		if(format < 4)
			throw Fault{VecFormatError, true};

		const uint32_t pc = read(sp + 4, Size::Long);
		m_a[7] = sp + 8 + (format - 4);
		setSR16(static_cast<uint16_t>(fvsr));
		m_cycles += 10;
		jump(pc);
	}

	void Cpu::opRts(const uint16_t)
	{
		const uint32_t pc = pop32();
		m_cycles += 5;
		jump(pc);
	}

	void Cpu::opMovec(const uint16_t)
	{
		requireSupervisor();
		const uint32_t ext = fetch16();
		const uint32_t reg = (ext >> 12) & 7;
		const uint32_t value = (ext & 0x8000) ? m_a[reg] : m_d[reg];
		const auto rc = static_cast<uint16_t>(ext & 0xfff);

		if(rc == 0x801)
			setVBR(value);
		else
			m_bus.writeControlRegister(rc, value);

		m_cycles += 9;
	}

	void Cpu::opJsr(const uint16_t _op)
	{
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;
		const uint32_t target = controlAddress(mode, reg);
		push32(m_pc);
		m_cycles += eaClass(mode, reg) == 2 ? 4 : 3;
		jump(target);
	}

	void Cpu::opJmp(const uint16_t _op)
	{
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;
		const uint32_t target = controlAddress(mode, reg);
		m_cycles += eaClass(mode, reg) == 2 ? 4 : 3;
		jump(target);
	}

	// ---------------------------------------------------------------------------------------------------
	// line 5

	void Cpu::opAddqSubq(const uint16_t _op)
	{
		uint32_t q = (_op >> 9) & 7;
		if(!q)
			q = 8;
		const bool isSub = (_op & 0x100) != 0;
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;

		if(mode == 1)
		{
			// address register destination: no condition codes
			m_a[reg] = isSub ? m_a[reg] - q : m_a[reg] + q;
			m_cycles += 1;
			return;
		}

		const Ea ea = decodeEa(mode, reg, Size::Long);
		const uint32_t v = readEa(ea, Size::Long);
		writeEa(ea, isSub ? sub(q, v, false) : add(q, v, false), Size::Long);

		const uint32_t cls = eaClass(mode, reg);
		m_cycles += cls == 0 ? 1 : (cls == 2 ? 4 : 3);
	}

	void Cpu::opScc(const uint16_t _op)
	{
		const uint32_t reg = _op & 7;
		m_d[reg] = (m_d[reg] & 0xffffff00) | (testCondition(_op >> 8) ? 0xff : 0x00);
		m_cycles += 1;
	}

	void Cpu::opTrapf(const uint16_t _op)
	{
		if(_op == 0x51fa)
			fetch16();
		else if(_op == 0x51fb)
			fetch32();
		m_cycles += 1;
	}

	// ---------------------------------------------------------------------------------------------------
	// line 6: branches

	void Cpu::opBcc(const uint16_t _op)
	{
		const uint32_t cc = (_op >> 8) & 0xf;
		const uint32_t base = m_pc;

		int32_t disp = static_cast<int8_t>(_op & 0xff);
		if(disp == 0)
			disp = static_cast<int16_t>(fetch16());
		else if(disp == -1)
			disp = static_cast<int32_t>(fetch32());

		const uint32_t target = base + static_cast<uint32_t>(disp);

		if(cc == 1)
		{
			// BSR
			push32(m_pc);
			m_cycles += 3;
			jump(target);
			return;
		}

		// UM table 3-11: backward branches are predicted taken, forward ones not taken
		const bool forward = disp >= 0;

		if(cc == 0)
		{
			m_cycles += 2;
			jump(target);
			return;
		}

		if(testCondition(cc))
		{
			m_cycles += forward ? 3 : 2;
			jump(target);
		}
		else
		{
			m_cycles += forward ? 1 : 3;
		}
	}

	void Cpu::opMoveq(const uint16_t _op)
	{
		const auto v = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(_op & 0xff)));
		m_d[(_op >> 9) & 7] = v;
		setLogicFlags(v, Size::Long);
		m_cycles += 1;
	}

	// ---------------------------------------------------------------------------------------------------
	// lines 8-d: two operand arithmetic and logic, all long

	namespace
	{
		// UM table 3-8, <ea>,Rx and Dy,<ea> forms of ADD/SUB/AND/OR/CMP/EOR
		uint32_t aluCycles(const uint32_t _eaClass, const bool _toMemory)
		{
			if(_toMemory)
				return _eaClass == 2 ? 4 : 3;
			switch(_eaClass)
			{
			case 0:		return 1;
			case 1:		return 3;
			default:	return 4;
			}
		}
	}

	void Cpu::opOr(const uint16_t _op)
	{
		const uint32_t dn = (_op >> 9) & 7;
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;
		const bool toMem = (_op & 0x100) != 0;

		const Ea ea = decodeEa(mode, reg, Size::Long);
		const uint32_t r = readEa(ea, Size::Long) | m_d[dn];
		if(toMem)
			writeEa(ea, r, Size::Long);
		else
			m_d[dn] = r;
		setLogicFlags(r, Size::Long);
		m_cycles += aluCycles(eaClass(mode, reg), toMem);
	}

	void Cpu::opAnd(const uint16_t _op)
	{
		const uint32_t dn = (_op >> 9) & 7;
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;
		const bool toMem = (_op & 0x100) != 0;

		const Ea ea = decodeEa(mode, reg, Size::Long);
		const uint32_t r = readEa(ea, Size::Long) & m_d[dn];
		if(toMem)
			writeEa(ea, r, Size::Long);
		else
			m_d[dn] = r;
		setLogicFlags(r, Size::Long);
		m_cycles += aluCycles(eaClass(mode, reg), toMem);
	}

	void Cpu::opEor(const uint16_t _op)
	{
		const uint32_t dn = (_op >> 9) & 7;
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;

		const Ea ea = decodeEa(mode, reg, Size::Long);
		const uint32_t r = readEa(ea, Size::Long) ^ m_d[dn];
		writeEa(ea, r, Size::Long);
		setLogicFlags(r, Size::Long);
		const uint32_t cls = eaClass(mode, reg);
		m_cycles += cls == 0 ? 1 : aluCycles(cls, true);
	}

	void Cpu::opSub(const uint16_t _op)
	{
		const uint32_t dn = (_op >> 9) & 7;
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;
		const bool toMem = (_op & 0x100) != 0;

		const Ea ea = decodeEa(mode, reg, Size::Long);
		const uint32_t v = readEa(ea, Size::Long);
		if(toMem)
			writeEa(ea, sub(m_d[dn], v, false), Size::Long);
		else
			m_d[dn] = sub(v, m_d[dn], false);
		m_cycles += aluCycles(eaClass(mode, reg), toMem);
	}

	void Cpu::opAdd(const uint16_t _op)
	{
		const uint32_t dn = (_op >> 9) & 7;
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;
		const bool toMem = (_op & 0x100) != 0;

		const Ea ea = decodeEa(mode, reg, Size::Long);
		const uint32_t v = readEa(ea, Size::Long);
		if(toMem)
			writeEa(ea, add(m_d[dn], v, false), Size::Long);
		else
			m_d[dn] = add(v, m_d[dn], false);
		m_cycles += aluCycles(eaClass(mode, reg), toMem);
	}

	void Cpu::opSuba(const uint16_t _op)
	{
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;
		const Ea ea = decodeEa(mode, reg, Size::Long);
		m_a[(_op >> 9) & 7] -= readEa(ea, Size::Long);
		m_cycles += aluCycles(eaClass(mode, reg), false);
	}

	void Cpu::opAdda(const uint16_t _op)
	{
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;
		const Ea ea = decodeEa(mode, reg, Size::Long);
		m_a[(_op >> 9) & 7] += readEa(ea, Size::Long);
		m_cycles += aluCycles(eaClass(mode, reg), false);
	}

	void Cpu::opCmp(const uint16_t _op)
	{
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;
		const Ea ea = decodeEa(mode, reg, Size::Long);
		cmp(readEa(ea, Size::Long), m_d[(_op >> 9) & 7]);
		m_cycles += aluCycles(eaClass(mode, reg), false);
	}

	void Cpu::opCmpa(const uint16_t _op)
	{
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;
		const Ea ea = decodeEa(mode, reg, Size::Long);
		cmp(readEa(ea, Size::Long), m_a[(_op >> 9) & 7]);
		m_cycles += aluCycles(eaClass(mode, reg), false);
	}

	void Cpu::opSubx(const uint16_t _op)
	{
		const uint32_t dx = (_op >> 9) & 7;
		m_d[dx] = sub(m_d[_op & 7], m_d[dx], true);
		m_cycles += 1;
	}

	void Cpu::opAddx(const uint16_t _op)
	{
		const uint32_t dx = (_op >> 9) & 7;
		m_d[dx] = add(m_d[_op & 7], m_d[dx], true);
		m_cycles += 1;
	}

	void Cpu::opMulW(const uint16_t _op)
	{
		const uint32_t dn = (_op >> 9) & 7;
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;
		const bool isSigned = (_op & 0x100) != 0;

		const Ea ea = decodeEa(mode, reg, Size::Word);
		const uint32_t src = readEa(ea, Size::Word);

		uint32_t r;
		if(isSigned)
			r = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(src)) * static_cast<int32_t>(static_cast<int16_t>(m_d[dn])));
		else
			r = (src & 0xffff) * (m_d[dn] & 0xffff);

		m_d[dn] = r;
		setLogicFlags(r, Size::Long);

		const uint32_t cls = eaClass(mode, reg);
		m_cycles += cls == 0 ? 9 : (cls == 2 ? 12 : 11);
	}

	void Cpu::opDivW(const uint16_t _op)
	{
		const uint32_t dn = (_op >> 9) & 7;
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;
		const bool isSigned = (_op & 0x100) != 0;

		const Ea ea = decodeEa(mode, reg, Size::Word);
		const uint32_t divisor = readEa(ea, Size::Word) & 0xffff;

		const uint32_t cls = eaClass(mode, reg);
		m_cycles += cls == 0 ? 20 : (cls == 2 ? 24 : 23);

		if(!divisor)
			throw Fault{VecDivideByZero, true};

		uint32_t q;
		uint32_t r;

		if(isSigned)
		{
			const int64_t a = static_cast<int32_t>(m_d[dn]);
			const int64_t b = static_cast<int16_t>(divisor);
			const int64_t quot = a / b;
			if(quot < -32768 || quot > 32767)
			{
				m_sr = static_cast<uint16_t>((m_sr & ~SrC) | SrV);
				return;
			}
			q = static_cast<uint32_t>(quot) & 0xffff;
			r = static_cast<uint32_t>(a % b) & 0xffff;
		}
		else
		{
			const uint32_t quot = m_d[dn] / divisor;
			if(quot > 0xffff)
			{
				m_sr = static_cast<uint16_t>((m_sr & ~SrC) | SrV);
				return;
			}
			q = quot;
			r = m_d[dn] % divisor;
		}

		m_d[dn] = (r << 16) | q;
		setLogicFlags(q, Size::Word);
	}

	// ---------------------------------------------------------------------------------------------------
	// line e: shifts, long register only

	void Cpu::opShift(const uint16_t _op)
	{
		const uint32_t reg = _op & 7;
		const bool left = (_op & 0x100) != 0;
		const bool logical = (_op & 0x08) != 0;

		uint32_t count;
		if(_op & 0x20)
		{
			count = m_d[(_op >> 9) & 7] & 63;
		}
		else
		{
			count = (_op >> 9) & 7;
			if(!count)
				count = 8;
		}

		const uint32_t v = m_d[reg];
		uint32_t r;
		bool carry = false;

		if(count == 0)
		{
			r = v;
		}
		else if(left)
		{
			// ASL and LSL are the same on the ColdFire, V is always cleared
			r = count >= 32 ? 0 : v << count;
			carry = count <= 32 && ((v >> (32 - count)) & 1);
		}
		else if(logical)
		{
			r = count >= 32 ? 0 : v >> count;
			carry = count <= 32 && ((v >> (count - 1)) & 1);
		}
		else
		{
			const auto s = static_cast<int32_t>(v);
			r = static_cast<uint32_t>(count >= 32 ? (s >> 31) : (s >> count));
			carry = count >= 32 ? (s < 0) : ((v >> (count - 1)) & 1);
		}

		m_d[reg] = r;

		uint16_t sr = static_cast<uint16_t>(m_sr & ~(SrN | SrZ | SrV | SrC));
		if(count)
		{
			sr &= ~SrX;
			if(carry)
				sr |= SrX | SrC;
		}
		if(r & 0x80000000)
			sr |= SrN;
		if(!r)
			sr |= SrZ;
		m_sr = sr;

		m_cycles += 1;
	}

	// ---------------------------------------------------------------------------------------------------
	// line f: cache and debug

	void Cpu::opCpushl(const uint16_t)
	{
		requireSupervisor();
		m_cycles += 1;
	}

	void Cpu::opWddata(const uint16_t _op)
	{
		// the operand is read and put on the debug data port, which nobody listens to
		const uint32_t sz = (_op >> 6) & 3;
		const Size size = sz == 0 ? Size::Byte : (sz == 1 ? Size::Word : Size::Long);
		const uint32_t mode = (_op >> 3) & 7;
		const uint32_t reg = _op & 7;
		const Ea ea = decodeEa(mode, reg, size);
		readEa(ea, size);
		m_cycles += eaClass(mode, reg) == 2 ? 4 : 3;
	}

	void Cpu::opWdebug(const uint16_t _op)
	{
		requireSupervisor();
		fetch16();
		const uint32_t addr = controlAddress((_op >> 3) & 7, _op & 7);
		read(addr, Size::Long);
		read(addr + 4, Size::Long);
		m_cycles += 5;
	}
}
