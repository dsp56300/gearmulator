#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace coldfire
{
	// What the core sees of the outside world. The chip model implements it: address decoding, wait
	// states, the interrupt acknowledge cycle and the MOVEC-only registers of the on-chip modules.
	class Bus
	{
	public:
		virtual ~Bus() = default;

		virtual uint8_t read8(uint32_t _addr) = 0;
		virtual uint16_t read16(uint32_t _addr) = 0;
		virtual uint32_t read32(uint32_t _addr) = 0;

		virtual void write8(uint32_t _addr, uint8_t _value) = 0;
		virtual void write16(uint32_t _addr, uint16_t _value) = 0;
		virtual void write32(uint32_t _addr, uint32_t _value) = 0;

		// instruction fetch, separate so that an implementation can model the instruction cache
		virtual uint16_t fetch16(const uint32_t _addr) { return read16(_addr); }

		// interrupt acknowledge cycle for a level, returns the vector number
		virtual uint8_t interruptAcknowledge(uint8_t _level) = 0;

		// MOVEC to a register outside of the core: CACR, ACR0/1, ROMBAR, RAMBAR, MBAR
		virtual void writeControlRegister(uint16_t _reg, uint32_t _value) {}

		// bus wait cycles accumulated since the last call, added to the instruction that caused them
		virtual uint32_t consumeWaitCycles() { return 0; }
	};

	// ColdFire V2 core as in the MCF5206e: ISA_A, hardware divide, MAC unit.
	// Instruction timing follows MCF5206e UM tables 3-5 to 3-11 (zero wait state memory); the bus adds
	// the wait states of the memory it decodes.
	class Cpu
	{
	public:
		enum Vector : uint8_t
		{
			VecAccessError			= 2,
			VecAddressError			= 3,
			VecIllegalInstruction	= 4,
			VecDivideByZero			= 5,
			VecPrivilegeViolation	= 8,
			VecTrace				= 9,
			VecLineA				= 10,
			VecLineF				= 11,
			VecFormatError			= 14,
			VecSpuriousInterrupt	= 24,
			VecAutovector1			= 25,
			VecTrap0				= 32
		};

		enum SrBits : uint16_t
		{
			SrC = 0x0001, SrV = 0x0002, SrZ = 0x0004, SrN = 0x0008, SrX = 0x0010,
			SrIpl = 0x0700, SrM = 0x1000, SrS = 0x2000, SrT = 0x8000,
			SrMask = SrT | SrS | SrM | SrIpl | SrX | SrN | SrZ | SrV | SrC
		};

		// called for opcodes that are defined but not implemented (MAC so far), before the exception is taken
		using UnimplementedCallback = std::function<void(uint32_t _pc, uint16_t _opcode)>;

		explicit Cpu(Bus& _bus);

		// reset exception: SR = $2700, VBR = 0, SP and PC from addresses 0 and 4
		void reset();

		// executes one instruction or takes one exception, returns the clock cycles used
		uint32_t step();

		// level (0-7) of the highest pending interrupt request, level 7 is edge sensitive
		void setInterruptLevel(uint8_t _level);

		uint32_t getD(const uint32_t _i) const { return m_d[_i]; }
		uint32_t getA(const uint32_t _i) const { return m_a[_i]; }
		void setD(const uint32_t _i, const uint32_t _v) { m_d[_i] = _v; }
		void setA(const uint32_t _i, const uint32_t _v) { m_a[_i] = _v; }

		uint32_t getPC() const { return m_pc; }
		void setPC(const uint32_t _pc) { m_pc = _pc; }

		uint16_t getSR() const { return m_sr; }
		void setSR(const uint16_t _sr) { m_sr = _sr & SrMask; }

		uint32_t getVBR() const { return m_vbr; }
		void setVBR(const uint32_t _vbr) { m_vbr = _vbr & 0xfff00000; }

		uint32_t getInstructionPC() const { return m_instructionPc; }

		bool isStopped() const { return m_stopped; }
		bool isHalted() const { return m_halted; }

		uint64_t getCycles() const { return m_totalCycles; }
		uint64_t getInstructionCount() const { return m_instructionCount; }

		void setUnimplementedCallback(UnimplementedCallback _cb) { m_unimplemented = std::move(_cb); }

		// called with the PC before every instruction, for tracing; costs a branch when not set
		void setInstructionHook(std::function<void(uint32_t _pc)> _hook) { m_instructionHook = std::move(_hook); }

	private:
		using Handler = void (Cpu::*)(uint16_t);

		// thrown by the effective address decoder and by instructions, caught in step()
		struct Fault
		{
			uint8_t vector;
			bool faultPc;	// true: stack the address of the faulting instruction, false: the next one
		};

		enum class Size { Byte = 1, Word = 2, Long = 4 };

		// effective address, computed once so that read-modify-write instructions touch (An)+/-(An) once
		struct Ea
		{
			enum class Kind { DataReg, AddrReg, Memory, Immediate } kind;
			uint32_t value;	// register index, memory address or immediate data
		};

		void buildTable();

		uint16_t fetch16();
		uint32_t fetch32();

		uint32_t read(uint32_t _addr, Size _size);
		void write(uint32_t _addr, uint32_t _value, Size _size);

		void push32(uint32_t _value);
		uint32_t pop32();

		// effective address handling, _mode and _reg are the 3-bit fields of the opcode
		Ea decodeEa(uint32_t _mode, uint32_t _reg, Size _size);
		uint32_t indexedAddress(uint32_t _base);
		uint32_t readEa(const Ea& _ea, Size _size);
		void writeEa(const Ea& _ea, uint32_t _value, Size _size);
		uint32_t controlAddress(uint32_t _mode, uint32_t _reg);

		// timing class of an effective address: 0 register or immediate, 1 memory, 2 indexed memory
		static uint32_t eaClass(uint32_t _mode, uint32_t _reg);

		void exception(uint8_t _vector, uint32_t _stackedPc, uint8_t _faultStatus = 0);
		void takeInterrupt(uint8_t _level);
		void requireSupervisor() const;
		void setSR16(uint16_t _sr);
		void jump(uint32_t _target);

		bool testCondition(uint32_t _cc) const;

		void setNZ(uint32_t _result, Size _size);
		void setLogicFlags(uint32_t _result, Size _size);

		uint32_t add(uint32_t _src, uint32_t _dst, bool _withX);
		uint32_t sub(uint32_t _src, uint32_t _dst, bool _withX);
		void cmp(uint32_t _src, uint32_t _dst);

		// instruction handlers
		void opIllegal(uint16_t _op);
		void opLineA(uint16_t _op);
		void opLineF(uint16_t _op);

		void opOriL(uint16_t _op);
		void opAndiL(uint16_t _op);
		void opSubiL(uint16_t _op);
		void opAddiL(uint16_t _op);
		void opEoriL(uint16_t _op);
		void opCmpiL(uint16_t _op);
		void opBitImm(uint16_t _op);
		void opBitReg(uint16_t _op);

		void opMove(uint16_t _op);
		void opMovea(uint16_t _op);

		void opNegxL(uint16_t _op);
		void opMoveFromSr(uint16_t _op);
		void opLea(uint16_t _op);
		void opClr(uint16_t _op);
		void opMoveFromCcr(uint16_t _op);
		void opNegL(uint16_t _op);
		void opMoveToCcr(uint16_t _op);
		void opNotL(uint16_t _op);
		void opMoveToSr(uint16_t _op);
		void opSwap(uint16_t _op);
		void opPea(uint16_t _op);
		void opExt(uint16_t _op);
		void opMovemToMem(uint16_t _op);
		void opMovemFromMem(uint16_t _op);
		void opTst(uint16_t _op);
		void opHalt(uint16_t _op);
		void opPulse(uint16_t _op);
		void opMulL(uint16_t _op);
		void opDivL(uint16_t _op);
		void opTrap(uint16_t _op);
		void opLink(uint16_t _op);
		void opUnlk(uint16_t _op);
		void opNop(uint16_t _op);
		void opStop(uint16_t _op);
		void opRte(uint16_t _op);
		void opRts(uint16_t _op);
		void opMovec(uint16_t _op);
		void opJsr(uint16_t _op);
		void opJmp(uint16_t _op);

		void opAddqSubq(uint16_t _op);
		void opScc(uint16_t _op);
		void opTrapf(uint16_t _op);

		void opBcc(uint16_t _op);
		void opMoveq(uint16_t _op);

		void opOr(uint16_t _op);
		void opDivW(uint16_t _op);
		void opSub(uint16_t _op);
		void opSuba(uint16_t _op);
		void opSubx(uint16_t _op);
		void opCmp(uint16_t _op);
		void opCmpa(uint16_t _op);
		void opEor(uint16_t _op);
		void opAnd(uint16_t _op);
		void opMulW(uint16_t _op);
		void opAdd(uint16_t _op);
		void opAdda(uint16_t _op);
		void opAddx(uint16_t _op);
		void opShift(uint16_t _op);

		void opCpushl(uint16_t _op);
		void opWddata(uint16_t _op);
		void opWdebug(uint16_t _op);

		Bus& m_bus;

		std::array<uint32_t, 8> m_d{};
		std::array<uint32_t, 8> m_a{};
		uint32_t m_pc = 0;
		uint16_t m_sr = 0x2700;
		uint32_t m_vbr = 0;

		uint32_t m_instructionPc = 0;
		uint32_t m_cycles = 0;

		uint8_t m_interruptLevel = 0;
		bool m_nmiEdge = false;			// level 7 went from below 7 to 7 and was not taken yet
		bool m_inhibitInterrupts = false;	// first instruction of an exception handler
		bool m_stopped = false;
		bool m_halted = false;

		uint64_t m_totalCycles = 0;
		uint64_t m_instructionCount = 0;

		UnimplementedCallback m_unimplemented;
		std::function<void(uint32_t)> m_instructionHook;

		std::array<Handler, 65536> m_table{};
	};
}
