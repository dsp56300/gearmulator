#pragma once

#include <cassert>
#include <cstdint>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef unsigned char uint8;
typedef signed char int8;
typedef unsigned short uint16;
typedef signed short int16;
typedef unsigned int uint32;
typedef signed int int32;

typedef struct _h8reg{
	union {
		struct {
			union {
				struct {
					int8 rl;
					int8 rh;
				};
				int16 r;
			};
			int16 e;
		};
		int er {0};
	};
} h8reg;

class H8SDevice
{
public:
	virtual ~H8SDevice() = default;
	virtual uint8_t read(uint32_t _addr) = 0;
	virtual void write(uint32_t _addr, uint8_t _val) = 0;
	void setState(class h8state* _state) {state = _state;}
protected:
	class h8state *state {nullptr};
};

static volatile bool g_dasm = true;

class h8state {
public:
	uint8 memory[1<<24] {};
	h8reg regs[8] {};
	uint8 *pc {0};		// program counter. pc is ALWAYS a multiple of 2. All instructions are 2bytes
	uint8 ccr {128};
	uint8 exr {0};
	H8SDevice* maps[1<<24] {};
	unsigned long long  cycles {0};
	unsigned long long pending_irqs {0};
	
	enum
	{
		ccr_c=1,	// Carry		//
		ccr_v=2,	// Overflow		// did arithmetic overflow occur?
		ccr_z=4,	// Zero			// 1 when last alu result was zero
		ccr_n=8,	// Negative		// top bit of last alu
		ccr_u=16,	// User bit
		ccr_h=32,	// Half-carry	// Was there a carry/borrow 4bits from the top of the last alu op
		ccr_ui=64,	// User bit
		ccr_i=128	// Interrupt mask bit (interrupts off)
	};
	
	int se8(int in) {in&=0xff;return (in&0x80)?(in|0xFFFFFF00):in;}
	int se16(int in) {in&=0xffff;return (in&0x8000)?(in|0xFFFF0000):in;}
	
	int8 &reg8(int rd) 	{return (rd&8)?regs[rd&7].rl:regs[rd&7].rh;}
	int16& reg16(int rd) {return (rd&8)?regs[rd&7].e:regs[rd&7].r;}
	int32& reg32(int rd)	{return regs[rd&7].er;}
	uint32 getSP() const	{return regs[7].er;}
	void setSP(uint32 s)	{regs[7].er=s;}
	const char *getRegName8(int r)	const {const char *names[16]={"R0h","R1h","R2h","R3h","R4h","R5h","R6h","R7h","R0l","R1l","R2l","R3l","R4l","R5l","R6l","R7l"};return names[r&15];}
	const char *getRegName16(int r)	const {const char *names[16]={"R0","R1","R2","R3","R4","R5","R6","R7","E0","E1","E2","E3","E4","E5","E6","E7"};return names[r&15];}
	const char *getRegName32(int r)	const {const char *names[8]={"ER0","ER1","ER2","ER3","ER4","ER5","ER6","ER7"};return names[r&7];}
	
	uint32 addr(uint8 *p) {return (uint32)(p-memory);}
	
	void boot()
	{
		pc = makepc(read32(0) & 0xffffff);
		cycles = pending_irqs = 0;
		ccr = 128; exr = 0;
	}
	
	uint8 *fail(uint8 *pc)
	{
		printf("INVALID INSTRUCTION AT %s\n",printAddr(addr(pc)));
		assert(false);
		return pc;
	}
	
	void memmap(H8SDevice *dev,int start,int len = 1)
	{
		dev->setState(this);
		for (int i=0;i<len;i++) maps[start+i]=dev;
	}
	
	void loadmem(const uint8* data,uint32_t size,uint32_t address)
	{
		memcpy(memory + address,data,size);
	}
	
	void readMemory(uint8* to, int from, int len)
	{
		memcpy(to, memory + from, len);
	}
	
	void interrupt(int which)
	{
		if (which >= 12 && which < 18) {
			uint8 ier = read8(0xfffff5);
			if (!(ier & (1 << (which - 12)))) return;
		}
		pending_irqs |= (1ULL << which);
	}
	
	int getPC() const {return pcoff(pc);}
	uint8 *makepc(int address) {return address+memory;}
	int pcoff(uint8* pc) const {return (int)(pc - memory);}
	
	// memory accesses should use these
	int32 read32(uint8 *from) {return read32((int)(from-memory));}
	int16 read16(uint8 *from) {return read16((int)(from-memory));}
	int8 read8(uint8 *from) {return read8((int)(from-memory));}
	
	void write32(int32 val,int to) {write16(val>>16,to);write16(val&0xffff,to+2);}
	uint32 read32(int from) {uint32 s=read16(from)&0xffff;s=(s<<16)|(read16(from+2)&0xffff);return s;}
	void write16(int16 val,int to) {write8(val>>8,to);write8(val&255,to+1);}
	uint16 read16(int from) {uint16 s=read8(from)&255;s=(s<<8)|(read8(from+1)&255);return s;}
	
	void write8(int8 byte,int to) {
		to&=0xffffff;
		clockMem(to, lastwrite);
		if (maps[to]) maps[to]->write(to, byte);
		else memory[to]=byte;
	}
	int8 read8(int from) {
		from&=0xffffff;
		clockMem(from, lastread);
		if (maps[from]) return maps[from]->read(from);
		return memory[from];
	}
	
	void pushL(uint32 val)	{uint32 sp=getSP()-4;write32(val,sp);setSP(sp);}
	uint32 popL() 			{uint32 sp=getSP();setSP(sp+4);return read32(sp);}
	void pushW(uint16 val)	{uint16 sp=getSP()-2;write16(val,sp);setSP(sp);}
	uint16 popW() 			{uint16 sp=getSP();setSP(sp+2);return read16(sp);}
	
	void pushPC(uint8 *pc,bool andccr=false)
	{
		uint32 p=(uint32)((pc-memory)&0xffffff);
		if (andccr) pushL(p | (ccr <<24)); else pushL(p);
	}
	uint8 *popPC(bool andccr=false) {
		uint32 p = popL();
		if (andccr) ccr = p>>24;
		return (p & 0xffffff) + memory;
	}
	
	uint8 *loadPCFrom(uint32 addr) {return (read32(addr)&0xFFFFFF)+memory;}
	
	void clockI(int I) {cycles += I;}
	void clockMem(int addr, int& last)
	{
		if (addr < 0x400000)	// rom and ram, 16bit reads, 3 states
		{
			if ((last & 1) || addr != (last | 1)) cycles += 3;
			last = addr;
		}
		else if (addr >= 0xFFFD10 && addr < 0xffffa0)	// on-chip ram / regs, 16bit, 1 state
		{
			if ((last & 1) || addr != (last | 1)) cycles++;
			last = addr;
		}
		else if (addr >= 0xffffa0)	// on-chip regs, 8bit, 1 state
		{
			cycles++;
			last = -1;
		}
		else	// off-chip, 8bit, 3 states.
		{
			cycles += 3;
			last = -1;
		}
	}
	
	unsigned long long getCycles() const {return cycles;}
	
	double real_time_in_sec() const {double d = static_cast<double>(cycles); return d / (16.0e6);}
	
	static const char *printAddr(int addr)
	{
		const char *names[256]={
			0,0,0,0,0,0,0,0, 	0,0,0,0,0,0,0,0,	// 0x00
			0,0,0,0,0,0,0,0, 	0,0,0,0,0,0,0,0,	// 0x10
			"MAR0AR", "MAR0AE", "MAR0AH", "MAR0AL", "ETCR0AH", "ETCR0AL", "IOAR0A", "DTCR0A", // 0x20
			"MAR0BR", "MAR0BE", "MAR0BH", "MAR0BL", "ETCR0BH", "ETCR0BL", "IOAR0B", "DTCR0B", // 0x28
			"MAR1AR", "MAR1AE", "MAR1AH", "MAR1AL", "ETCR1AH", "ETCR1AL", "IOAR1A", "DTCR1A", // 0x30
			"MAR1BR", "MAR1BE",	"MAR1BH", "MAR1BL", "ETCR1BH", "ETCR1BL", "IOAR1B", "DTCR1B", // 0x38
			0,0,0,0,0,0,0,0, 	0,0,0,0,0,0,0,0,	// 0x40
			0,0,0,0,0,0,0,0, 	0,0,0,0,0,0,0,0,	// 0x50
			"TSTR", "TSNC", "TMDR", "TFCR", "TCR0", "TIOR0", "TIER0", "TSR0", // 0x60
			"TCNT0H", "TCNT0L", "GRA0H", "GRA0L", "GRB0H", "GRB0L", "TCR1", "TIOR1", // 0x68
			"TIER1", "TSR1", "TCNT1H", "TCNT1L", "GRA1H", "GRA1L", "GRB1H", "GRB1L", // 0x70
			"TCR2", "TIOR2", "TIER2", "TSR2", "TCNT2H", "TCNT2L", "GRA2H", "GRA2L",	// 0x78
			"GRB2H", "GRB2L", "TCR3", "TIOR3", "TIER3", "TSR3", "TCNT3H", "TCNT3L", // 0x80
			"GRA3H", "GRA3L", "GRB3H", "GRB3L", "BRA3H", "BRA3L", "BRB3H", "BRB3L", // 0x88
			"TOER", "TOCR", "TCR4", "TIOR4", "TIER4", "TSR4", "TCNT4H", "TCNT4L", // 0x90
			"GRA4H", "GRA4L", "GRB4H", "GRB4L", "BRA4H", "BRA4L", "BRB4H", "BRB4L", // 0x98
			"TPMR", "TPCR", "NDERB", "NDERA", "NDRB", "NDRA", "NDRB", "NDRA", // 0xa0
			"TCSR", "TCNT", 0, "RSTCSR", "RFSHCR", "RTMCSR", "RTCNT", "RTCOR", // 0xa8
			"SMR0", "BRR0", "SCR0", "TDR0", "SSR0", "RDR0", 0, 0, // 0xb0
			"SMR1", "BRR1", "SCR1", "TDR1", "SSR1", "RDR1", 0, 0, // 0xb8
			0,0,0,0,0, "P4DDR", 0, "P4DR", // 0xc0
			0, "P6DDR", 0, "P6DR", 0, "P8DDR", "P7DR", "P8DR", // 0xc8
			"P9DDR", "PADDR", "P9DR", "PADR", "PBDDR", 0, "PBDR", 0, // 0xd0
			0,0, "P4PCR", 0,0,0,0,0, // 0xd8
			"ADDRAH", "ADDRAL", "ADDRBH", "ADDRBL", "ADDRCH", "ADDRCL", "ADDRDH", "ADDRDL", // 0xe0
			"ADCSR", "ADCR", 0, 0, "ABWCR", "ASTCR", "WCR", "WCER", // 0xe8
			0, "MDCR", "SYSCR", "BRCR", "ISCR", "IER", "ISR", 0, // 0xf0
			"IPRA", "IPRB", 0, 0, 0, 0, 0, 0	// 0xf8
		};
		if (((addr&0xffff00) == 0xffff00) && names[addr&0xff]) return names[addr&0xff];
		static char buffer[32];
		snprintf(buffer,32,"#0x%06x",addr);
		return buffer;
	}
	
	void dumpRegs()
	{
		printf("%06x: ",addr(pc));
		for (int i=0;i<8;i++) printf("%08x, ",regs[i].er);
		printf(" [%02x, %02x]\n",ccr,exr);
	}
	
	void dumpStack()
	{
		printf("Stack trace (%06x):\n", getSP());
		for (int i = -4; i < 16; i++)
			printf("%06x: %08x\n", getSP() - i * 4, read32(getSP() - i * 4));
	}
	void dumpRAM()
	{
		for (int i = 0x200000; i < 0x240000; i += 4)
			if (read32(i))
				printf("%06x: %08x\n", i, read32(i));
	}
protected:
	void ccrflags_val(int value)
	{
		ccrflags_set(ccr_v,0);
		ccrflags_set(ccr_n,value<0);
		ccrflags_set(ccr_z,!value);
	}
	
	void ccrflags_set(int bit,int value) {ccr&=~bit;if (value)ccr|=bit;}

	int lastread {-1}, lastwrite {-1};
};

template<bool execute,bool disassemble>
class H8S : public h8state
{
public:
	void step()
	{
		pc = handle_instr(pc);
	}
	uint8 *handle_instr(uint8 *_pc)
	{
		pc = _pc;
		if (execute && pending_irqs)
		{
			bool ue = read8(0xfffff2) & 8;
			int i = ccr & 128; //, ue = (read8(0xfffff2) & 8), u = ccr & 16;
//			if (!ue && i) i = (ccr & ccr_u);	// TODO: why does this have to be disabled?
			if (!i)
			{
				int which = 0; while (!(pending_irqs & (1ULL << which))) which++;
				pending_irqs ^= (1ULL << which);
				pushPC(pc,true);
				clockI(4);
				ccrflags_set(ccr_i, 1);
				if (!ue) ccrflags_set(ccr_u, 1);
				
				pc = loadPCFrom(which << 2);	// Offset for TRAP calls is 0x20 + 4 * trap number.
				indent++;
			}

		}
		if (disassemble && g_dasm) for (int i = 0; i < indent; i++) printf(" ");
		if (((long long)pc)&1) pc--;	// ALWAYS mask off the bottom bit when loading short or int.
		uint8 firstbyte=read8(pc);pc++;
		switch((firstbyte>>4)&15)
		{
		case 0: return handle_instr_0(firstbyte,pc);
		case 1: return handle_instr_1(firstbyte,pc);
		case 2: return handle_movba(firstbyte,pc);
		case 3: return handle_movba(firstbyte,pc);
		case 4: return handle_bcc(firstbyte,pc);
		case 5: return handle_instr_5(firstbyte,pc);
		case 6: return handle_instr_6(firstbyte,pc);
		case 7:	return handle_instr_7(firstbyte,pc);
		case 8: return handle_addi(firstbyte,pc);
		case 9:	return handle_addxi(firstbyte,pc);
		case 10:return handle_cmpi(firstbyte,pc);
		case 11:return handle_subxi(firstbyte,pc);
		case 12:return handle_ori(firstbyte,pc);
		case 13:return handle_xori(firstbyte,pc);
		case 14:return handle_andi(firstbyte,pc);
		case 15:return handle_movbi(firstbyte,pc);
		}
		return 0;
	}
protected:
	uint8 *handle_instr_0(uint8 op,uint8 *pc)
	{
		switch (op&15)
		{
		case 0:	return handle_nop(op,pc);
		case 1:	return handle_instr_01(op,pc);
		case 2:	return handle_stcr(op,pc);
		case 3:	return handle_ldcr(op,pc);
		case 4:	return handle_orc(op,pc);
		case 5:	return handle_xorc(op,pc);
		case 6:	return handle_andc(op,pc);
		case 7:	return handle_ldci(op,pc);
		case 8: return handle_addr(op,pc);
		case 9: return handle_addrw(op,pc);
		case 10:return handle_instr_0a(op,pc);
		case 11:return handle_instr_0b(op,pc);
		case 12:return handle_movr(op,pc);
		case 13:return handle_movrw(op,pc);
		case 14:return handle_addxr(op,pc);
		case 15:return handle_instr_0f(op,pc);
		}
		return 0;
	}
	uint8 *handle_instr_1(uint8 op,uint8 *pc)
	{
		switch (op&15)
		{
		case 0:	return handle_instr_10(op,pc);
		case 1:	return handle_instr_11(op,pc);
		case 2:	return handle_instr_12(op,pc);
		case 3:	return handle_instr_13(op,pc);
		case 4:	return handle_orr(op,pc);
		case 5:	return handle_xorr(op,pc);
		case 6:	return handle_andr(op,pc);
		case 7:	return handle_instr_17(op,pc);
		case 8:	return handle_subr(op,pc);
		case 9:	return handle_subrw(op,pc);
		case 10:return handle_instr_1a(op,pc);
		case 11:return handle_instr_1b(op,pc);
		case 12:return handle_cmpr(op,pc);
		case 13:return handle_cmprw(op,pc);
		case 14:return handle_subxr(op,pc);
		case 15:return handle_instr_1f(op,pc);
		}
		return 0;
	}
	uint8 *handle_instr_5(uint8 op,uint8 *pc)
	{
		switch (op&15)
		{
		case 0:	return handle_mulxu(op,pc);
		case 1:	return handle_divxu(op,pc);
		case 2:	return handle_mulxuw(op,pc);
		case 3:	return handle_divxuw(op,pc);
		case 4:	return handle_rts(op,pc);
		case 5: return handle_bsr(op,pc);
		case 6:	return handle_rte(op,pc);
		case 7: return handle_trapa(op,pc);
		case 8:	return handle_bccw(op,pc);
		case 9:	return handle_jmpae(op,pc);
		case 10:return handle_jmpimm(op,pc);
		case 11:return handle_jmpaa(op,pc);
		case 12:return handle_bsrw(op,pc);
		case 13:return handle_jsrae(op,pc);
		case 14:return handle_jsrimm(op,pc);
		case 15:return handle_jsraa(op,pc);
		}
		return 0;
	}
	uint8 *handle_instr_6(uint8 op,uint8 *pc)
	{
		switch (op&15)
		{
		case 0: return handle_bsetr(op,pc);
		case 1: return handle_bnotr(op,pc);
		case 2: return handle_bclrr(op,pc);
		case 3: return handle_btstr(op,pc);
		case 4:	return handle_orrw(op,pc);
		case 5:	return handle_xorrw(op,pc);
		case 6:	return handle_andrw(op,pc);
		case 7:	return handle_bsti(op,pc);
		case 8: return handle_movbae(op,pc);
		case 9:	return handle_movwae(op,pc);
		case 10:return handle_instr_6a(op,pc);
		case 11:return handle_movwaa(op,pc);
		case 12:return handle_movbaep(op,pc);
		case 13:return handle_movwaep(op,pc);
		case 14:return handle_movbaeo(op,pc);
		case 15:return handle_movwaeo(op,pc);
		}
		return 0;
	}
	uint8 *handle_instr_7(uint8 op,uint8 *pc)
	{
		switch (op&15)
		{
		case 0: return handle_bseti(op,pc);
		case 1: return handle_bnoti(op,pc);
		case 2: return handle_bclri(op,pc);
		case 3: return handle_btsti(op,pc);
		case 4:	return handle_bor(op,pc);
		case 5:	return handle_bxor(op,pc);
		case 6: return handle_band(op,pc);
		case 7: return handle_bld(op,pc);
		case 8: return handle_movbwaed(op,pc);
		case 9:	return handle_instr_79(op,pc);
		case 10:return handle_instr_7a(op,pc);
		case 11:return handle_eepmov(op,pc);
		case 12:return handle_bitstuff_1(op,pc);
		case 13:return handle_bitstuff_1(op,pc);
		case 14:return handle_bitstuff_1(op,pc);
		case 15:return handle_bitstuff_1(op,pc);
		}
		return 0;
	}
	uint8 *handle_instr_01(uint8 op,uint8 *pc)
	{
		int b=read8(pc);pc++;
		switch ((b>>4)&15)
		{
		case 0:	return handle_movl(op,b,pc);
		case 1:	return handle_ldmstm(op,b,pc);
		case 2:	return handle_ldmstm(op,b,pc);
		case 3:	return handle_ldmstm(op,b,pc);
		case 4:	return handle_ldcstc(op,b,pc);
		case 8: return handle_sleep(op,b,pc);
		case 12:return handle_instr_01c(op,b,pc);
		case 13:return handle_instr_01d(op,b,pc);
		case 14:return handle_tas(op,b,pc);
		case 15:return handle_instr_01f(op,b,pc);
		default:return fail(pc-2);	// 0x6 is MAC instruction, 0xA is CLRMAC instruction.
		}
	}
	uint8 *handle_instr_0a(uint8 op,uint8 *pc)
	{
		int b=read8(pc);pc++;
		switch ((b>>4)&15)
		{
		case 0:	return handle_incb(op,b,pc);
		case 8:
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
		case 14:
		case 15:return handle_addl(op,b,pc);
		default:return fail(pc-2);
		}
	}
	uint8 *handle_instr_0b(uint8 op,uint8 *pc)
	{
		int b=read8(pc);pc++;
		switch ((b>>4)&15)
		{
		case 0:	return handle_addsi(op,b,pc,1);
		case 5: return handle_incwi(op,b,pc,1);
		case 7: return handle_incli(op,b,pc,1);
		case 8:	return handle_addsi(op,b,pc,2);
		case 9:	return handle_addsi(op,b,pc,4);
		case 13:return handle_incwi(op,b,pc,2);
		case 15:return handle_incli(op,b,pc,2);
		default:return fail(pc-2);
		}
	}
	uint8 *handle_instr_0f(uint8 op,uint8 *pc)
	{
		int b=read8(pc);pc++;
		switch ((b>>4)&15)
		{
		case 0:	return handle_daa(op,b,pc);
		case 8:
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
		case 14:
		case 15:return handle_movrl(op,b,pc);
		default:return fail(pc-2);
		}
	}
	uint8 *handle_instr_10(uint8 op,uint8 *pc)
	{
		int b=read8(pc);pc++;
		int top=(b>>4)&7; if (top==2 || top==6) return fail(pc-2);
		if (b&128) 	return handle_shal(op, b, pc);
		else		return handle_shll(op, b, pc);
	}
	uint8 *handle_instr_11(uint8 op,uint8 *pc)
	{
		int b=read8(pc);pc++;
		int top=(b>>4)&7; if (top==2 || top==6) return fail(pc-2);
		if (b&128) 	return handle_shar(op, b, pc);
		else		return handle_shlr(op, b, pc);
	}
	uint8 *handle_instr_12(uint8 op,uint8 *pc)
	{
		int b=read8(pc);pc++;
		int top=(b>>4)&7; if (top==2 || top==6) return fail(pc-2);
		if (b&128) 	return handle_rotl(op, b, pc);
		else		return handle_rotxl(op, b, pc);
	}
	uint8 *handle_instr_13(uint8 op,uint8 *pc)
	{
		int b=read8(pc);pc++;
		int top=(b>>4)&7; if (top==2 || top==6) return fail(pc-2);
		if (b&128) 	return handle_rotr(op, b, pc);
		else		return handle_rotxr(op, b, pc);
	}
	uint8 *handle_instr_17(uint8 op,uint8 *pc)
	{
		int b=read8(pc);pc++;
		switch ((b>>4)&15)
		{
		case 0:
		case 1:
		case 3:	return handle_not(op, b, pc);
		case 5:
		case 7:	return handle_extu(op, b, pc);
		case 8:
		case 9:
		case 11:return handle_neg(op, b, pc);
		case 13:
		case 15:return handle_exts(op, b, pc);
		default:return fail(pc-2);
		}
	}
	uint8 *handle_instr_1a(uint8 op,uint8 *pc)
	{
		int b=read8(pc);pc++;
		if (b&0x80) return handle_subl(op, b, pc);
		if (!(b&0xf0)) return handle_decb(op, b, pc);
		return fail(pc-2);
	}
	uint8 *handle_instr_1b(uint8 op,uint8 *pc)
	{
		int b=read8(pc);pc++;
		switch ((b>>4)&15)
		{
		case 0:	return handle_subsi(op, b, pc, 1);
		case 5:	return handle_decwi(op, b, pc, 1);
		case 7:	return handle_decli(op, b, pc, 1);
		case 8:	return handle_subsi(op, b, pc, 2);
		case 9:	return handle_subsi(op, b, pc, 4);
		case 13:return handle_decwi(op, b, pc, 2);
		case 15:return handle_decli(op, b, pc, 2);
		default:return fail(pc-2);
		}
	}
	uint8 *handle_instr_1f(uint8 op,uint8 *pc)
	{
		int b=read8(pc);pc++;
		if (b&0x80) return handle_cmpl(op, b, pc);
		if (!(b&0xf0)) return handle_das(op, b, pc);
		return fail(pc-2);
	}
	uint8 *handle_instr_6a(uint8 op,uint8 *pc)
	{
		int b=read8(pc);pc++;
		switch ((b>>4)&15)
		{
		case 0:	return handle_movbaa(op, b, pc);
		case 1:	return handle_bitstuff_2(op, b, pc);
		case 2:	return handle_movbaa(op, b, pc);
		case 3:	return handle_bitstuff_2(op, b, pc);
		case 4:	return fail(pc-2);	// MOVFPE instruction
		case 8:	return handle_movbaa(op, b, pc);
		case 10:return handle_movbaa(op, b, pc);
		case 12:return fail(pc-2);	// MOVTPE instruction
		default:return fail(pc-2);
		}
	}
	uint8 *handle_instr_79(uint8 op,uint8 *pc)
	{
		int b=read8(pc);pc++;
		switch ((b>>4)&15)
		{
		case 0:	return handle_movwi(op, b, pc);
		case 1:	return handle_addwi(op, b, pc);
		case 2: return handle_cmpwi(op, b, pc);
		case 3: return handle_subwi(op, b, pc);
		case 4: return handle_orwi(op, b, pc);
		case 5:	return handle_xorwi(op, b, pc);
		case 6:	return handle_andwi(op, b, pc);
		default:return fail(pc-2);
		}
	}
	uint8 *handle_instr_7a(uint8 op,uint8 *pc)
	{
		int b=read8(pc);pc++;
		switch ((b>>4)&15)
		{
		case 0:	return handle_movli(op, b, pc);
		case 1:	return handle_addli(op, b, pc);
		case 2: return handle_cmpli(op, b, pc);
		case 3: return handle_subli(op, b, pc);
		case 4: return handle_orli(op, b, pc);
		case 5:	return handle_xorli(op, b, pc);
		case 6:	return handle_andli(op, b, pc);
		default:return fail(pc-2);
		}
	}
	uint8 *handle_instr_01c(uint8 op, uint8 b, uint8 *pc)
	{
		if (b&15) return fail(pc-2);
		int c=read8(pc);pc++;
		int d=read8(pc);pc++;
		if (((c>>4)&15)!=5) return fail(pc-4);
		switch (c&15)
		{
		case 0:	return handle_mulxs(d, pc);
		case 2:	return handle_mulxsw(d, pc);
		return fail(pc-4);
		}
		return 0;
	}
	uint8 *handle_instr_01d(uint8 op, uint8 b, uint8 *pc)
	{
		if (b&15) return fail(pc-2);
		int c=read8(pc);pc++;
		int d=read8(pc);pc++;
		if (((c>>4)&15)!=5) return fail(pc-4);
		switch (c&15)
		{
		case 1:	return handle_divxs(d, pc);
		case 3:	return handle_divxsw(d, pc);
		return fail(pc-4);
		}
		return 0;
	}
	uint8 *handle_instr_01f(uint8 op, uint8 b, uint8 *pc)
	{
		if (b&15) return fail(pc-2);
		int c=read8(pc);pc++;
		int d=read8(pc);pc++;
		if (((c>>4)&15)!=6) return fail(pc-4);
		if (d&0x88) return fail(pc-4);
		switch (c&15)
		{
		case 4:	return handle_orrl(d, pc);
		case 5: return handle_xorrl(d, pc);
		case 6: return handle_andrl(d, pc);
		default:return fail(pc-4);
		}
		return 0;
	}


	//////// MOV.B @immediate,Reg
	uint8 *handle_movba(uint8 op,uint8 *pc)
	{
		int rd=op&15,imm=((read8(pc))&255)+0xFFFF00;pc++;op=(op>>4)&15;
		if (disassemble)
		{
			if (op==2)	dasm(pc-2,"MOV.B","@%s,%s",printAddr(imm),getRegName8(rd));
			else		dasm(pc-2,"MOV.B","%s,@%s",getRegName8(rd),printAddr(imm));
		}
		if (!execute) return pc;
		int8 &r=reg8(rd);
		if (op==2)	{r=read8(imm);ccrflags_val(r);}
		else		{ccrflags_val(r);write8(r,imm);}
		return pc;
	}
	
	uint8 *handle_movbaa(uint8 op,uint8 b,uint8 *pc)
	{
		uint8 *startpc=pc-2;int rd=b&15,regsrc=(b&0x80)?1:0,size=(b&0x20)?1:0;
		int imm=0;if (size) {imm=read32(pc);pc+=4;} else {imm=se16(read16(pc));pc+=2;}
		if (disassemble)
		{
			if (!regsrc)	dasm(startpc,"MOV.B","@%s,%s",printAddr(imm),getRegName8(rd));
			else			dasm(startpc,"MOV.B","%s,@%s",getRegName8(rd),printAddr(imm));
		}
		if (!execute) return pc;
		int8 &r=reg8(rd);
		if (!regsrc)	{r=read8(imm);ccrflags_val(r);}
		else			{ccrflags_val(r);write8(r,imm);}
		return pc;
	}

	uint8 *handle_movbae(uint8 op,uint8 *pc)
	{
		int r=read8(pc);pc++;int er=(r>>4)&7,d=(r>>7)&1;r&=15;
		if (disassemble)
		{
			if (d)	dasm(pc-2,"MOV.B","%s,@%s",getRegName8(r),getRegName32(er));
			else	dasm(pc-2,"MOV.B","@%s,%s",getRegName32(er),getRegName8(r));
		}
		if (!execute) return pc;
		if (d)	write8(reg8(r),reg32(er));
		else	reg8(r)=read8(reg32(er));
		ccrflags_val(reg8(r));
		return pc;
	}
	uint8 *handle_movwae(uint8 op,uint8 *pc)
	{
		int r=read8(pc);pc++;int er=(r>>4)&7,d=(r>>7)&1;r&=15;
		if (disassemble)
		{
			if (d)	dasm(pc-2,"MOV.W","%s,@%s",getRegName16(r),getRegName32(er));
			else	dasm(pc-2,"MOV.W","@%s,%s",getRegName32(er),getRegName16(r));
		}
		if (!execute) return pc;
		if (d)	write16(reg16(r),reg32(er));
		else	reg16(r)=read16(reg32(er));
		ccrflags_val(reg16(r));
		return pc;
	}
	uint8 *handle_movwaa(uint8 op,uint8 *pc)
	{
		uint8 *ipc=pc-1;int r=read8(pc);pc++;int sub=(r>>4)&15;r&=15;int d=sub&8;
		int offset=read16(pc);pc+=2;
		if (sub&5)	return fail(pc);	// valid values are 0,2,8,10
		if (sub&2) {offset=(offset<<16)|(read16(pc) & 0xffff);pc+=2;} else offset=se16(offset);
		if (disassemble)
		{
			if (d)	dasm(ipc,"MOV.W","%s,@%s",getRegName16(r),printAddr(offset));
			else	dasm(ipc,"MOV.W","@%s,%s",printAddr(offset),getRegName16(r));
		}
		if (!execute) return pc;
		if (d) write16(reg16(r),offset); else reg16(r)=read16(offset);
		ccrflags_val(reg16(r));
		return pc;
	}
	uint8 *handle_movbaep(uint8 op,uint8 *pc)
	{
		int r=read8(pc);pc++;int e=(r>>4)&7,d=(r>>7)&1;r&=15;
		if (disassemble)
		{
			if (d)	dasm(pc-2,"MOV.B","%s,@-%s",getRegName8(r),getRegName32(e));
			else	dasm(pc-2,"MOV.B","@%s+,%s",getRegName32(e),getRegName8(r));
		}
		if (!execute) return pc;
		if (d) {reg32(e)--;write8(reg8(r),reg32(e));}
		else	{reg8(r)=read8(reg32(e));reg32(e)++;}
		ccrflags_val(reg8(r));
		clockI(2);
		return pc;
	}
	uint8 *handle_movwaep(uint8 op,uint8 *pc)
	{
		int r=read8(pc);pc++;int e=(r>>4)&7,d=(r>>7)&1;r&=15;
		if (disassemble)
		{
			if (e==7)	dasm(pc-2,d?"PUSH.W":"POP.W","%s",getRegName16(r));
			else if (d)	dasm(pc-2,"MOV.W","%s,@-%s",getRegName16(r),getRegName32(e));
			else		dasm(pc-2,"MOV.W","@%s+,%s",getRegName32(e),getRegName16(r));
		}
		if (!execute) return pc;
		if (d) {reg32(e)-=2;write16(reg16(r),reg32(e));}
		else	{reg16(r)=read16(reg32(e));reg32(e)+=2;}
		clockI(2);
		ccrflags_val(reg16(r));
		return pc;
	}
	uint8 *handle_movbaeo(uint8 op,uint8 *pc)
	{
		int r=read8(pc);pc++;int e=(r>>4)&7,d=(r>>7)&1;r&=15;int offset=read16(pc);pc+=2;
		if (disassemble)
		{
			if (d)	dasm(pc-4,"MOV.B","%s,@(#0x%x,%s)",getRegName8(r),offset,getRegName32(e));
			else	dasm(pc-4,"MOV.B","@(#0x%x,%s),%s",offset,getRegName32(e),getRegName8(r));
		}
		if (!execute) return pc;
		if (d) {write8(reg8(r),reg32(e)+offset);}
		else	{reg8(r)=read8(reg32(e)+offset);}
		ccrflags_val(reg8(r));
		return pc;
	}
	uint8 *handle_movwaeo(uint8 op,uint8 *pc)
	{
		int r=read8(pc);pc++;int e=(r>>4)&7,d=(r>>7)&1;r&=15;int offset=read16(pc);pc+=2;
		if (disassemble)
		{
			if (d)	dasm(pc-4,"MOV.W","%s,@(#0x%x,%s)",getRegName16(r),offset,getRegName32(e));
			else	dasm(pc-4,"MOV.W","@(#0x%x,%s),%s",offset,getRegName32(e),getRegName16(r));
		}
		if (!execute) return pc;
		if (d) {write16(reg16(r),reg32(e)+offset);}
		else	{reg16(r)=read16(reg32(e)+offset);}
		ccrflags_val(reg16(r));
		return pc;
	}
	uint8 *handle_movbwaed(uint8 op,uint8 *pc)
	{
		int r=read8(pc);pc++;int e=(r>>4);
		if (r&0x8f)	return fail(pc-2);
		r=read8(pc);pc++;int w=r&1;
		if ((r&0xfe)!=0x6a) return fail(pc-3);
		r=read8(pc);pc++;int op2=(r>>4)&15;r&=15;
		if ((op2&7)!=2) return fail(pc-4);
		int off=read32(pc);pc+=4;int d=(op2&8);
		if (disassemble)
		{
			if (w)
			{
				if (d)	dasm(pc-8,"MOV.W","%s,@(#0x%x,%s)",getRegName16(r),off,getRegName32(e));
				else	dasm(pc-8,"MOV.W","@(#0x%x,%s),%s",off,getRegName32(e),getRegName16(r));
			}
			else
			{
				if (d)	dasm(pc-8,"MOV.B","%s,@(#0x%x,%s)",getRegName8(r),off,getRegName32(e));
				else	dasm(pc-8,"MOV.B","@(#0x%x,%s),%s",off,getRegName32(e),getRegName8(r));
			}
		}
		if (!execute) return pc;
		if (w)
		{
			if (d) write16(reg16(r),reg32(e)+off);	else reg16(r)=read16(reg32(e)+off);
			ccrflags_val(reg16(r));
		}
		else
		{
			if (d) write8(reg8(r),reg32(e)+off);	else reg8(r)=read8(reg32(e)+off);
			ccrflags_val(reg8(r));
		}
		return pc;
	}
	
	uint8* handle_movl(uint8 op,uint8 b,uint8 *pc)
	{
		uint8 *pcstart=pc-2;
		if (b) return fail(pcstart);
		uint8 c=read8(pc);pc++;
		if ((c&0xe8)!=0x68) return fail(pcstart);
		int d=read8(pc); pc++;
		if (c==0x6b)	// immediate
		{
			if (d&0x58) return fail(pcstart);	// these bits must be zero.
			int reg=d&7,srcisreg=(d&0x80)?1:0,addr=0;
			if (d&0x20)	{addr=read32(pc);pc+=4;} else {addr=se16(read16(pc));pc+=2;}
			if (disassemble)
			{
				if (srcisreg)	dasm(pcstart,"MOV.L","%s,@%s",getRegName32(reg),printAddr(addr));
				else			dasm(pcstart,"MOV.L","@%s,%s",printAddr(addr),getRegName32(reg));
			}
			if (execute)
			{
				if (srcisreg) 	{write32(reg32(reg),addr);ccrflags_val(reg32(reg));}
				else			{reg32(reg)=read32(addr);ccrflags_val(reg32(reg));}
			}
			return pc;
		}
		if (d&0x8) return fail(pcstart);	// must be zero
		if (c==0x6d)	// with predec/postinc
		{
			int rd=d&7,rs=(d>>4)&7,predec=(d&0x80)?1:0,pushpop=0;
			if (rs==7) pushpop=1;
			if (predec) {int t=rs;rs=rd;rd=t;}
			if (disassemble)
			{
				if (pushpop)dasm(pcstart,predec?"PUSH.L":"POP.L","%s",getRegName32(predec?rs:rd));
				else		dasm(pcstart,"MOV.L",predec?"%s,@-%s":"@%s+,%s",getRegName32(rs),getRegName32(rd));
			}
			if (execute)
			{
				if (predec) {reg32(rd)-=4;write32(reg32(rs),reg32(rd));	ccrflags_val(reg32(rs));}
				else		{reg32(rd)=read32(reg32(rs));reg32(rs)+=4;	ccrflags_val(reg32(rd));}
				clockI(2);
			}
			return pc;
		}
		int dispsize=(c==0x69)?0:(c==0x6f)?2:(c==0x78)?4:-1,disp=0;
		if (dispsize==-1) return fail(pcstart);
		int rd=(d&7),rs=(d>>4)&7,srcisreg=(d&0x80)?1:0;

		if (dispsize==2)	{disp=read16(pc);pc+=2;}
		else if (dispsize==4)
		{
			int e=read8(pc);pc++;	if (e!=0x6B) return fail(pcstart);
			int f=read8(pc);pc++;	if ((f&0x58) || !(f&0x20)) return fail(pcstart);
			srcisreg=(f&0x80)?1:0;rd=f&7;
			disp=read32(pc);pc+=4;
		}
		if (srcisreg) {int t=rs;rs=rd;rd=t;}
		if (disassemble)
		{
			if (!dispsize)		dasm(pcstart,"MOV.L",srcisreg?"%s,@%s":"@%s,%s",getRegName32(rs),getRegName32(rd));
			else if (srcisreg)	dasm(pcstart,"MOV.L","%s,@(%s,%s)",getRegName32(rs),printAddr(disp),getRegName32(rd));
			else				dasm(pcstart,"MOV.L","@(%s,%s),%s",printAddr(disp),getRegName32(rs),getRegName32(rd));
		}
		if (execute)
		{
			if (srcisreg)	{write32(reg32(rs),disp+reg32(rd));	ccrflags_val(reg32(rs));}
			else			{reg32(rd)=read32(disp+reg32(rs));	ccrflags_val(reg32(rd));}
		}
		return pc;
	}
	
	uint8 *handle_movrl(uint8 op,uint8 b,uint8 *pc)
	{
		if (b&0x8) return fail(pc-2);
		int rd=(b&7),rs=(b>>4)&7;
		if (disassemble)	dasm(pc-2,"MOV.L","%s,%s",getRegName32(rs),getRegName32(rd));
		if (execute) {int32 v=reg32(rs);ccrflags_val(v);reg32(rd)=v;}
		return pc;
	}
	
	//////// LDM/STM
	uint8* handle_ldmstm(uint8 op,uint8 b,uint8 *pc)
	{
		if (b&7) return fail(pc-2);	// must be zero
		int c=read8(pc);pc++;
		if (c!=0x6D)	return fail(pc-3);
		int d=read8(pc);pc++;
		if ((d&0x78)!=0x70) return fail(pc-4);
		int ern=d&7,store=(d&0x80)?1:0,num=(b>>4)&7;
		int er1=store?ern:ern-num,er2=store?ern+num:ern;
		if (disassemble) dasm(pc-4,store?"STM.L":"LDM.L",store?"(%s-%s),@-SP":"@SP+,(%s-%s)",getRegName32(er1),getRegName32(er2));
		if (execute)
		{
			if (store) 	for (int i=er1;i<=er2;i++) pushL(reg32(i));
			else		for (int i=er2;i>=er1;i--) reg32(i)=popL();
			clockI(1);
		}
		return pc;
	}
	
	uint8* handle_ldcstc(uint8 op,uint8 b,uint8 *pc)
	{
		uint8 *pcstart=pc-2;
		if ((b&0xfe)!=0x40) return fail(pcstart);
		int isexr=(b&1);	// if not exr then ccr
		uint8 &cr=isexr?exr:ccr;
		int c=read8(pc);pc++;
		int d=read8(pc);pc++;
		if (isexr)
		{
			if (c==7)	// this is the weirdly placed LDC imm,exr
			{
				if (disassemble) dasm(pcstart,"LDC","#0x%02X,EXR", d&255);
				if (execute) exr=d;
				return pc;
			}
			if (c==4)
			{
				if (disassemble) dasm(pcstart,"ORC","#0x%02X,EXR", d&255);
				if (execute) exr|=d;
				return pc;
			}
			if (c==5)
			{
				if (disassemble) dasm(pcstart,"XORC","#0x%02X,EXR", d&255);
				if (execute) exr^=d;
				return pc;
			}
			if (c==6)
			{
				if (disassemble) dasm(pcstart,"ANDC","#0x%02X,EXR", d&255);
				if (execute) exr&=d;
				return pc;
			}
		}
		if (c==0x6b)
		{
			if (d&0x5f) return fail(pcstart);
			int addr=0,store=(d&0x80)?1:0;
			if (d&0x20) {addr=read32(pc);pc+=4;} else {addr=se16(read16(pc));pc+=2;}
			if (disassemble)
			{
				if (store)	dasm(pcstart,"STC","%s,@%s",isexr?"EXR":"CCR",printAddr(addr));
				else		dasm(pcstart,"LDC","@%s,%s",printAddr(addr),isexr?"EXR":"CCR");
			}
			if (execute)
			{
				if (store) write16(cr,addr);	else cr=static_cast<uint8_t>(read16(addr));
			}
			return pc;
		}
		if (d&0xf)	return fail(pcstart);
		if (c==0x6d)	// predec/postinc
		{
			int r=(d>>4)&7,predec=(d&0x80)?1:0;
			if (disassemble)
			{
				if (predec)	dasm(pcstart,"STC.W","%s,@-%s",isexr?"EXR":"CCR",getRegName32(r));
				else		dasm(pcstart,"LDC.W","@%s+,%s",getRegName32(r),isexr?"EXR":"CCR");
			}
			if (execute)
			{
				if (predec) {reg32(r)-=2;write16(cr,reg32(r));}
				else		{cr=static_cast<uint8_t>(read16(reg32(r)));reg32(r)+=2;}
				clockI(2);
			}
			return pc;
		}
		int dispsize=(c==0x69)?0:(c==0x6f)?2:(c==0x78)?4:-1,disp=0;
		if (dispsize==-1) return fail(pcstart);
		int r=(d>>4)&7,storingcr=(d&0x80)?1:0;

		if (dispsize==2)	{disp=read16(pc);pc+=2;}
		else if (dispsize==4)
		{
			int e=read8(pc);pc++;	if (e!=0x6B) return fail(pcstart);
			int f=read8(pc);pc++;	if ((f&0x5F) || !(f&0x20)) return fail(pcstart);
			storingcr=(f&0x80)?1:0;
			disp=read32(pc);pc+=4;
		}
		if (disassemble)
		{
			if (!dispsize && storingcr)	dasm(pcstart,"STC","%s,@%s",isexr?"EXR":"CCR",getRegName32(r));
			else if (!dispsize)			dasm(pcstart,"LDC","@%s,%s",getRegName32(r),isexr?"EXR":"CCR");
			else if (storingcr)			dasm(pcstart,"STC","%s,@(%s,%s)",isexr?"EXR":"CCR",printAddr(disp),getRegName32(r));
			else						dasm(pcstart,"LDC","@(%s,%s),%s",printAddr(disp),getRegName32(r),isexr?"EXR":"CCR");
		}
		if (execute)
		{
			if (storingcr)	write16(cr,disp+reg32(r));
			else			cr=static_cast<uint8_t>(read16(disp + reg32(r)));
		}
		return pc;
	}
	
	uint8 *handle_tas(uint8 op,uint8 b,uint8 *pc)
	{
		if (b&0xf) return fail(pc-2);
		int c=read8(pc);pc++;
		if (c!=0x7b) return fail(pc-3);
		int d=read8(pc);pc++;
		if ((d&0x8f)!=0xc) return fail(pc-4);
		int r=(d>>4)&7;
		if (disassemble)	dasm(pc-4,"TAS","@%s",getRegName32(r));
		if (execute)	{uint8 b=read8(reg32(r));ccrflags_val(b);write8(b|0x80,reg32(r));}
		return pc;
	}
	
	//////// REGISTER OPS
	uint8 *handle_movr(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (disassemble)	dasm(pc-2,"MOV.B","%s,%s",getRegName8(rs),getRegName8(rd));
		if (execute) {int8 &src=reg8(rs), &dst=reg8(rd);	ccrflags_val(src);	dst=src; }
		return pc;
	}
	uint8 *handle_movrw(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (disassemble)	dasm(pc-2,"MOV.W","%s,%s",getRegName16(rs),getRegName16(rd));
		if (execute) {int16 &src=reg16(rs),&dst=reg16(rd);	ccrflags_val(src);	dst=src; }
		return pc;
	}

	uint8 *handle_orr(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (disassemble)	dasm(pc-2,"OR.B","%s,%s",getRegName8(rs),getRegName8(rd));
		if (execute) {int8 &src=reg8(rs),&dst=reg8(rd);	dst|=src;	ccrflags_val(dst); }
		return pc;
	}

	uint8 *handle_orrw(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (disassemble)	dasm(pc-2,"OR.W","%s,%s",getRegName16(rs),getRegName16(rd));
		if (execute) {int16 &src=reg16(rs),&dst=reg16(rd);	dst|=src;	ccrflags_val(dst); }
		return pc;
	}
	uint8 *handle_orrl(uint8 d,uint8 *pc)
	{
		int rs=(d>>4)&15,rd=(d)&15;
		if (disassemble)	dasm(pc-4,"OR.L","%s,%s",getRegName32(rs),getRegName32(rd));
		if (execute) {int32 &src=reg32(rs),&dst=reg32(rd);	dst|=src;	ccrflags_val(dst); }
		return pc;
	}

	uint8 *handle_xorr(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (disassemble)	dasm(pc-2,"XOR.B","%s,%s",getRegName8(rs),getRegName8(rd));
		if (execute) {int8 &src=reg8(rs),&dst=reg8(rd);	dst^=src;	ccrflags_val(dst); }
		return pc;
	}

	uint8 *handle_xorrw(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (disassemble)	dasm(pc-2,"XOR.W","%s,%s",getRegName16(rs),getRegName16(rd));
		if (execute) {int16 &src=reg16(rs),&dst=reg16(rd);	dst^=src;	ccrflags_val(dst); }
		return pc;
	}
	uint8 *handle_xorrl(uint8 d,uint8 *pc)
	{
		int rs=(d>>4)&15,rd=(d)&15;
		if (disassemble)	dasm(pc-4,"XOR.L","%s,%s",getRegName32(rs),getRegName32(rd));
		if (execute) {int32 &src=reg32(rs),&dst=reg32(rd);	dst^=src;	ccrflags_val(dst); }
		return pc;
	}

	uint8 *handle_andr(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (disassemble)	dasm(pc-2,"AND.B","%s,%s",getRegName8(rs),getRegName8(rd));
		if (execute) {int8 &src=reg8(rs),&dst=reg8(rd);	dst&=src;	ccrflags_val(dst); }
		return pc;
	}

	uint8 *handle_andrw(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (disassemble)	dasm(pc-2,"AND.W","%s,%s",getRegName16(rs),getRegName16(rd));
		if (execute) {int16 &src=reg16(rs),&dst=reg16(rd);	dst&=src;	ccrflags_val(dst); }
		return pc;
	}
	uint8 *handle_andrl(uint8 d,uint8 *pc)
	{
		int rs=(d>>4)&15,rd=(d)&15;
		if (disassemble)	dasm(pc-4,"AND.L","%s,%s",getRegName32(rs),getRegName32(rd));
		if (execute) {int32 &src=reg32(rs),&dst=reg32(rd);	dst&=src;	ccrflags_val(dst); }
		return pc;
	}

	uint8 *handle_incb(uint8 op,uint8 b,uint8 *pc)
	{
		int r=b&0xf;
		if (disassemble) dasm(pc-2,"INC.B","%s",getRegName8(r));
		if (execute) {int v = reg8(r)+1; ccrflags_val(v); ccrflags_set(ccr_v, (~reg8(r) & v) & 0x80); reg8(r) = v;}
		return pc;
	}
	uint8 *handle_decb(uint8 op,uint8 b,uint8 *pc)
	{
		int r=b&0xf;
		if (disassemble) dasm(pc-2,"DEC.B","%s",getRegName8(r));
		if (execute) {int v = reg8(r) - 1; ccrflags_val(v); ccrflags_set(ccr_v, (reg8(r) & ~v) & 0x80); reg8(r) = v;}
		return pc;
	}
	
	uint8 *handle_incwi(uint8 op,uint8 b,uint8 *pc,int imm)
	{
		int r=b&0xf;
		if (disassemble) dasm(pc-2,"INC.W","#%d,%s",imm,getRegName16(r));
		if (execute) {int v = reg16(r)+imm; ccrflags_val(v); ccrflags_set(ccr_v, (~reg16(r) & v) & 0x8000); reg16(r) = v;}
		return pc;
	}
	uint8 *handle_decwi(uint8 op,uint8 b,uint8 *pc,int imm)
	{
		int r=b&0xf;
		if (disassemble) dasm(pc-2,"DEC.W","#%d,%s",imm,getRegName16(r));
		if (execute) {int v = reg16(r) - imm; ccrflags_val(v); ccrflags_set(ccr_v, (reg16(r) & ~v) & 0x8000); reg16(r) = v;}
		return pc;
	}

	uint8 *handle_incli(uint8 op,uint8 b,uint8 *pc,int imm)
	{
		if (b&0x8) return fail(pc-2);
		int r=b&0xf;
		if (disassemble) dasm(pc-2,"INC.L","#%d,%s",imm,getRegName32(r));
		if (execute) {int v = reg32(r)+imm; ccrflags_val(v); ccrflags_set(ccr_v, (~reg32(r) & v) & 0x80000000); reg32(r) = v;}
		return pc;
	}
	uint8 *handle_decli(uint8 op,uint8 b,uint8 *pc,int imm)
	{
		if (b&0x8) return fail(pc-2);
		int r=b&0xf;
		if (disassemble) dasm(pc-2,"DEC.L","#%d,%s",imm,getRegName32(r));
		if (execute) {int v = reg32(r) - imm; ccrflags_val(v); ccrflags_set(ccr_v, (reg32(r) & ~v) & 0xff000000); reg32(r)=v;}
		return pc;

	}
	
	uint8 *handle_addr(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (disassemble)	dasm(pc-2,"ADD.B","%s,%s",getRegName8(rs),getRegName8(rd));
		if (execute) reg8(rd)=add8(reg8(rd),reg8(rs));
		return pc;
	}

	uint8 *handle_addxr(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (disassemble)	dasm(pc-2,"ADDX.B","%s,%s",getRegName8(rs),getRegName8(rd));
		if (execute) reg8(rd)=add8(reg8(rd),reg8(rs),ccr,1);
		return pc;
	}

	uint8 *handle_addrw(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (disassemble)	dasm(pc-2,"ADD.W","%s,%s",getRegName16(rs),getRegName16(rd));
		if (execute) reg16(rd)=add16(reg16(rd),reg16(rs));
		return pc;
	}
	
	uint8 *handle_addsi(uint8 op,uint8 b,uint8 *pc,int imm)
	{
		if (b&0x8) return fail(pc-2);
		int rd=(b&7);
		if (disassemble)	dasm(pc-2,"ADDS","#0x%x,%s",imm,getRegName32(rd));
		if (execute) reg32(rd)=add32(reg32(rd),imm,true);
		return pc;
	}
	
	uint8 *handle_subsi(uint8 op,uint8 b,uint8 *pc,int imm)
	{
		if (b&0x8) return fail(pc-2);
		int rd=(b&7);
		if (disassemble)	dasm(pc-2,"SUBS","#0x%x,%s",imm,getRegName32(rd));
		if (execute) reg32(rd)=sub32(reg32(rd),imm,true);
		return pc;
	}

	uint8 *handle_addl(uint8 op,uint8 b,uint8 *pc)
	{
		if (b&0x8) return fail(pc-2);
		int rs=(b>>4)&7,rd=(b&7);
		if (disassemble)	dasm(pc-2,"ADD.L","%s,%s",getRegName32(rs),getRegName32(rd));
		if (execute) reg32(rd)=add32(reg32(rd),reg32(rs));
		return pc;
	}
	
	uint8 *handle_subr(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (disassemble)	dasm(pc-2,"SUB.B","%s,%s",getRegName8(rs),getRegName8(rd));
		if (execute) reg8(rd)=sub8(reg8(rd),reg8(rs));
		return pc;
	}

	uint8 *handle_subxr(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (disassemble)	dasm(pc-2,"SUBX.B","%s,%s",getRegName8(rs),getRegName8(rd));
		if (execute) reg8(rd)=sub8(reg8(rd),reg8(rs),ccr,1);
		return pc;
	}

	uint8 *handle_subrw(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (disassemble)	dasm(pc-2,"SUB.W","%s,%s",getRegName16(rs),getRegName16(rd));
		if (execute) reg16(rd)=sub16(reg16(rd),reg16(rs));
		return pc;
	}
	uint8 *handle_subl(uint8 op,uint8 b,uint8 *pc)
	{
		if (b&8) return fail(pc-2);
		int rd=b&7,rs=(b>>4)&7;
		if (disassemble)	dasm(pc-2,"SUB.L","%s,%s",getRegName32(rs),getRegName32(rd));
		if (execute)		reg32(rd)=sub32(reg32(rd),reg32(rs));
		return pc;
	}
	uint8 *handle_cmpl(uint8 op,uint8 b,uint8 *pc)
	{
		if (b&8) return fail(pc-2);
		int rd=b&7,rs=(b>>4)&7;
		if (disassemble)	dasm(pc-2,"CMP.L","%s,%s",getRegName32(rs),getRegName32(rd));
		if (execute)		sub32(reg32(rd),reg32(rs));
		return pc;
	}
	
	uint8 *handle_cmpr(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (disassemble)	dasm(pc-2,"CMP.B","%s,%s",getRegName8(rs),getRegName8(rd));
		if (execute) sub8(reg8(rd),reg8(rs));
		return pc;
	}
	uint8 *handle_cmprw(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (disassemble)	dasm(pc-2,"CMP.W","%s,%s",getRegName16(rs),getRegName16(rd));
		if (execute) sub16(reg16(rd),reg16(rs));
		return pc;
	}
	
	uint8 *handle_mulxu(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (disassemble)	dasm(pc-2,"MULXU.B","%s,%s",getRegName8(rs),getRegName16(rd));
		if (execute) {int a=(reg8(rs)&255),b=(reg16(rd)&255);int16 &d=reg16(rd);d=a*b; clockI(12);}
		return pc;
	}
	uint8 *handle_mulxs(uint8 d,uint8 *pc)
	{
		int rs=(d>>4)&15,rd=(d)&15;
		if (disassemble)	dasm(pc-4,"MULXS.B","%s,%s",getRegName8(rs),getRegName16(rd));
		if (execute) {int a=reg8(rs),b=se8(reg16(rd));int16 &d=reg16(rd);d=a*b;ccrflags_set(ccr_n,d<0);ccrflags_set(ccr_z,!d); clockI(12);}
		return pc;
	}
	uint8 *handle_divxu(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (rd&8) printf("UNDEFINED BEHAVIOUR. Attempting to divide E register\n");
		if (disassemble)	dasm(pc-2,"DIVXU.B","%s,%s",getRegName8(rs),getRegName16(rd));
		if (execute)
		{
			unsigned int a=(reg8(rs)&255),b=(reg16(rd)&65535), c = 0;
			ccrflags_set(ccr_n,a&128);ccrflags_set(ccr_z,!a);
			if (a!=0) c=b/a;
			else printf("Divide by zero\n");
			reg8((rd&7)+8)=c;
			reg8(rd&7)=b-c*a;
			clockI(12);
		}
		return pc;
	}
	uint8 *handle_divxs(uint8 d,uint8 *pc)
	{
		int rs=(d>>4)&15,rd=(d)&15;
		if (rd&8) printf("UNDEFINED BEHAVIOUR. Attempting to divide E register\n");
		if (disassemble)	dasm(pc-4,"DIVXS.B","%s,%s",getRegName8(rs),getRegName16(rd));
		if (execute)
		{
			int a=reg8(rs),b=reg16(rd), c = 0;
			ccrflags_set(ccr_z,!a);
			if (a!=0) c=b/a;
			else printf("Divide by zero\n");
			reg8((rd&7)+8)=c;
			reg8(rd&7)=b-c*a;
			ccrflags_set(ccr_n, ((a & ~b) | (~a & b)) & 0x80);
			clockI(12);
		}
		return pc;
	}
		
	uint8 *handle_mulxuw(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (rd&8) return fail(pc-2);
		if (disassemble)	dasm(pc-2,"MULXU.W","%s,%s",getRegName16(rs),getRegName32(rd));
		if (execute) {uint32 a=(reg16(rs)&65535),b=(reg32(rd)&65535);reg32(rd)=a*b; clockI(20);}
		return pc;
	}
	uint8 *handle_mulxsw(uint8 d,uint8 *pc)
	{
		int rs=(d>>4)&15,rd=(d)&15;
		if (rd&8) return fail(pc-4);
		if (disassemble)	dasm(pc-4,"MULXS.W","%s,%s",getRegName16(rs),getRegName32(rd));
		if (execute) {int a=reg16(rs),b=se16(reg32(rd));int32 &d=reg32(rd);d=a*b;ccrflags_set(ccr_n,d<0);ccrflags_set(ccr_z,!d); clockI(20);}
		return pc;
	}
	uint8 *handle_divxuw(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (rd&8) return fail(pc-2);
		if (disassemble)	dasm(pc-2,"DIVXU.W","%s,%s",getRegName16(rs),getRegName32(rd));
		if (execute)
		{
			uint32 a=reg16(rs)&65535,b=reg32(rd), c = 0;
			ccrflags_set(ccr_n,a&32768);ccrflags_set(ccr_z,!a);
			if (a!=0) c=b/a;
			else printf("Divide by zero\n");
			reg16(rd)=c;
			reg16(rd+8)=b-c*a;
			clockI(20);
		}
		return pc;
	}
	uint8 *handle_divxsw(uint8 d,uint8 *pc)
	{
		int rs=(d>>4)&15,rd=(d)&15;
		if (rd&8) return fail(pc-4);
		if (disassemble)	dasm(pc-4,"DIVXS.W","%s,%s",getRegName16(rs),getRegName32(rd));
		if (execute)
		{
			clockI(20);
			int32 a=se16(reg16(rs)),b=reg32(rd), c = 1;
			ccrflags_set(ccr_z,!a);
			if (a!=0) c=b/a;
			else if (b) {printf("Divide by zero\n"); return pc;}
			reg16(rd)=c;
			reg16(rd+8)=b-c*a;
			ccrflags_set(ccr_n, ((a & ~b) | (~a & b)) & 0x8000);
		}
		return pc;
	}
	
	//////// Shifts
	uint8 *handle_shal(uint8 op, uint8 b, uint8 *pc)
	{
		int shift=(b&64)?2:1, size=((b>>4)&3)+1, r=(b&15);	// size=1,2,4
		if (size==4 && (r&8)) return fail(pc-2);
		if (disassemble)
		{
			if (size==1) dasm(pc-2,"SHAL.B",(shift==2)?"#2,%s":"%s",getRegName8(r));
			if (size==2) dasm(pc-2,"SHAL.W",(shift==2)?"#2,%s":"%s",getRegName16(r));
			if (size==4) dasm(pc-2,"SHAL.L",(shift==2)?"#2,%s":"%s",getRegName32(r));
		}
		if (!execute) return pc;
		uint32 v=0;
		if (size==1) v=reg8(r); else if (size==2) v=reg16(r); else if (size==4)	v=reg32(r);
		const int bits=size*8;
		int vt=v>>((bits-1)-shift);
		ccrflags_set(ccr_v,vt!=0 && vt!=((2<<shift)-1));
		ccrflags_set(ccr_c,(v&((1<<(bits-shift)))));
		v<<=shift;
		ccrflags_set(ccr_z,!v);
		ccrflags_set(ccr_n,v&(1<<(bits-1)));
		if (size==1) reg8(r)=v&0xff; else if (size==2) reg16(r)=v&0xffff; else if (size==4) reg32(r)=v;
		return pc;
	}

	uint8 *handle_shll(uint8 op, uint8 b, uint8 *pc)
	{
		int shift=(b&64)?2:1, size=((b>>4)&3)+1, r=(b&15);	// size=1,2,4
		if (size==4 && (r&8)) return fail(pc-2);
		if (disassemble)
		{
			if (size==1) dasm(pc-2,"SHLL.B",(shift==2)?"#2,%s":"%s",getRegName8(r));
			if (size==2) dasm(pc-2,"SHLL.W",(shift==2)?"#2,%s":"%s",getRegName16(r));
			if (size==4) dasm(pc-2,"SHLL.L",(shift==2)?"#2,%s":"%s",getRegName32(r));
		}
		if (!execute) return pc;
		uint32 v=0;
		if (size==1) v=reg8(r)&0xff; else if (size==2) v=reg16(r)&0xffff; else if (size==4)	v=reg32(r);
		const int bits=size*8;
		ccrflags_set(ccr_c,(v&((1<<(bits-shift)))));
		v<<=shift;
		ccrflags_val(v<<(8*(4-size)));
		if (size==1) reg8(r)=v&0xff; else if (size==2) reg16(r)=v&0xffff; else if (size==4) reg32(r)=v;
		return pc;
	}
	uint8 *handle_shar(uint8 op, uint8 b, uint8 *pc)
	{
		int shift=(b&64)?2:1, size=((b>>4)&3)+1, r=(b&15);	// size=1,2,4
		if (size==4 && (r&8)) return fail(pc-2);
		if (disassemble)
		{
			if (size==1) dasm(pc-2,"SHAR.B",(shift==2)?"#2,%s":"%s",getRegName8(r));
			if (size==2) dasm(pc-2,"SHAR.W",(shift==2)?"#2,%s":"%s",getRegName16(r));
			if (size==4) dasm(pc-2,"SHAR.L",(shift==2)?"#2,%s":"%s",getRegName32(r));
		}
		if (!execute) return pc;
		int32 v=0;
		if (size==1) v=reg8(r); else if (size==2) v=reg16(r); else if (size==4)	v=reg32(r);
		ccrflags_set(ccr_c,(v&((1<<(shift-1)))));
		v>>=shift;
		ccrflags_val(v);
		if (size==1) reg8(r)=(v)&0xff; else if (size==2) reg16(r)=(v)&0xffff; else if (size==4) reg32(r)=v;
		return pc;
	}

	uint8 *handle_shlr(uint8 op, uint8 b, uint8 *pc)
	{
		int shift=(b&64)?2:1, size=((b>>4)&3)+1, r=(b&15);	// size=1,2,4
		if (size==4 && (r&8)) return fail(pc-2);
		if (disassemble)
		{
			if (size==1) dasm(pc-2,"SHLR.B",(shift==2)?"#2,%s":"%s",getRegName8(r));
			if (size==2) dasm(pc-2,"SHLR.W",(shift==2)?"#2,%s":"%s",getRegName16(r));
			if (size==4) dasm(pc-2,"SHLR.L",(shift==2)?"#2,%s":"%s",getRegName32(r));
		}
		if (!execute) return pc;
		uint32 v=0;
		if (size==1) v=reg8(r)&0xff; else if (size==2) v=reg16(r)&0xffff; else if (size==4)	v=reg32(r);
		ccrflags_set(ccr_c,(v&((1<<(shift-1)))));
		v>>=shift;
		ccrflags_val(v<<(8*(4-size)));
		if (size==1) reg8(r)=v&0xff; else if (size==2) reg16(r)=v&0xffff; else if (size==4) reg32(r)=v;
		return pc;
	}

	uint8 *handle_rotl(uint8 op, uint8 b, uint8 *pc)
	{
		int shift=(b&64)?2:1, size=((b>>4)&3)+1, r=(b&15);	// size=1,2,4
		if (size==4 && (r&8)) return fail(pc-2);
		if (disassemble)
		{
			if (size==1) dasm(pc-2,"ROTL.B",(shift==2)?"#2,%s":"%s",getRegName8(r));
			if (size==2) dasm(pc-2,"ROTL.W",(shift==2)?"#2,%s":"%s",getRegName16(r));
			if (size==4) dasm(pc-2,"ROTL.L",(shift==2)?"#2,%s":"%s",getRegName32(r));
		}
		if (!execute) return pc;
		uint32 v=0;
		if (size==1) v=reg8(r)&0xff; else if (size==2) v=reg16(r)&0xffff; else if (size==4)	v=reg32(r);
		uint32 t=v>>(8*size-shift);
		v<<=shift;v|=t;
		ccrflags_set(ccr_c,t&1);
		ccrflags_val(v<<(8*(4-size)));
		if (size==1) reg8(r)=v&0xff; else if (size==2) reg16(r)=v&0xffff; else if (size==4) reg32(r)=v;
		return pc;
	}

	uint8 *handle_rotxl(uint8 op, uint8 b, uint8 *pc)
	{
		int shift=(b&64)?2:1, size=((b>>4)&3)+1, r=(b&15);	// size=1,2,4
		if (size==4 && (r&8)) return fail(pc-2);
		if (disassemble)
		{
			if (size==1) dasm(pc-2,"ROTXL.B",(shift==2)?"#2,%s":"%s",getRegName8(r));
			if (size==2) dasm(pc-2,"ROTXL.W",(shift==2)?"#2,%s":"%s",getRegName16(r));
			if (size==4) dasm(pc-2,"ROTXL.L",(shift==2)?"#2,%s":"%s",getRegName32(r));
		}
		if (!execute) return pc;
		uint32 v=0;
		if (size==1) v=reg8(r)&0xff; else if (size==2) v=reg16(r)&0xffff; else if (size==4)	v=reg32(r);
		for (int i=0;i<shift;i++)
		{
			uint32 nc=(v>>(8*size-1))&1;
			v<<=1;if (ccr & ccr_c) v|=1;
			ccrflags_set(ccr_c,nc);
		}
		ccrflags_val(v<<(8*(4-size)));
		if (size==1) reg8(r)=v&0xff; else if (size==2) reg16(r)=v&0xffff; else if (size==4) reg32(r)=v;
		return pc;
	}
	uint8 *handle_rotr(uint8 op, uint8 b, uint8 *pc)
	{
		int shift=(b&64)?2:1, size=((b>>4)&3)+1, r=(b&15);	// size=1,2,4
		if (size==4 && (r&8)) return fail(pc-2);
		if (disassemble)
		{
			if (size==1) dasm(pc-2,"ROTR.B",(shift==2)?"#2,%s":"%s",getRegName8(r));
			if (size==2) dasm(pc-2,"ROTR.W",(shift==2)?"#2,%s":"%s",getRegName16(r));
			if (size==4) dasm(pc-2,"ROTR.L",(shift==2)?"#2,%s":"%s",getRegName32(r));
		}
		if (!execute) return pc;
		uint32 v=0;
		if (size==1) v=reg8(r)&0xff; else if (size==2) v=reg16(r)&0xffff; else if (size==4)	v=reg32(r);
		uint32 t=v&((1<<shift)-1);
		v>>=shift;
		v|=t<<(8*size-shift);
		ccrflags_set(ccr_c,(t>>(shift-1))&1);
		ccrflags_val(v<<(8*(4-size)));
		if (size==1) reg8(r)=v&0xff; else if (size==2) reg16(r)=v&0xffff; else if (size==4) reg32(r)=v;
		return pc;
	}

	uint8 *handle_rotxr(uint8 op, uint8 b, uint8 *pc)
	{
		int shift=(b&64)?2:1, size=((b>>4)&3)+1, r=(b&15);	// size=1,2,4
		if (size==4 && (r&8)) return fail(pc-2);
		if (disassemble)
		{
			if (size==1) dasm(pc-2,"ROTXR.B",(shift==2)?"#2,%s":"%s",getRegName8(r));
			if (size==2) dasm(pc-2,"ROTXR.W",(shift==2)?"#2,%s":"%s",getRegName16(r));
			if (size==4) dasm(pc-2,"ROTXR.L",(shift==2)?"#2,%s":"%s",getRegName32(r));
		}
		if (!execute) return pc;
		uint32 v=0;
		if (size==1) v=reg8(r)&0xff; else if (size==2) v=reg16(r)&0xffff; else if (size==4)	v=reg32(r);
		for (int i=0;i<shift;i++)
		{
			uint32 r=v&1;
			v>>=1;
			if (ccr & ccr_c) v|=1<<(8*size-1);
			ccrflags_set(ccr_c,r);
		}
		ccrflags_val(v<<(8*(4-size)));
		if (size==1) reg8(r)=v&0xff; else if (size==2) reg16(r)=v&0xffff; else if (size==4) reg32(r)=v;
		return pc;
	}
	
	//////// BITWISE OPS
	uint8 *handle_bsetr(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (disassemble)	dasm(pc-2,"BSET","%s,%s",getRegName8(rs),getRegName8(rd));
		if (execute)		reg8(rd)|=1<<(reg8(rs)&7);
		return pc;
	}
	uint8 *handle_bseti(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int imm=(regs>>4)&15,rd=(regs)&15;
		if (imm&8) return fail(pc-2);
		if (disassemble)	dasm(pc-2,"BSET","#%d,%s",imm,getRegName8(rd));
		if (execute)		reg8(rd)|=1<<imm;
		return pc;
	}

	uint8 *handle_bnotr(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (disassemble)	dasm(pc-2,"BNOT","%s,%s",getRegName8(rs),getRegName8(rd));
		if (execute)		reg8(rd)^=1<<(reg8(rs)&7);
		return pc;
	}
	uint8 *handle_bnoti(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int imm=(regs>>4)&15,rd=(regs)&15;
		if (imm&8) return fail(pc-2);
		if (disassemble)	dasm(pc-2,"BNOT","#%d,%s",imm,getRegName8(rd));
		if (execute)		reg8(rd)^=1<<imm;
		return pc;
	}

	uint8 *handle_bclrr(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (disassemble)	dasm(pc-2,"BCLR","%s,%s",getRegName8(rs),getRegName8(rd));
		if (execute)		reg8(rd)&=~(1<<(reg8(rs)&7));
		return pc;
	}
	uint8 *handle_bclri(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int imm=(regs>>4)&15,rd=(regs)&15;
		if (imm&8) return fail(pc-2);
		if (disassemble)	dasm(pc-2,"BCLR","#%d,%s",imm,getRegName8(rd));
		if (execute)		reg8(rd)&=~(1<<imm);
		return pc;
	}

	uint8 *handle_btstr(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int rs=(regs>>4)&15,rd=(regs)&15;
		if (disassemble)	dasm(pc-2,"BTST","%s,%s",getRegName8(rs),getRegName8(rd));
		if (execute)		ccrflags_set(ccr_z,!(reg8(rd)&(1<<(reg8(rs)&7))));
		return pc;
	}
	uint8 *handle_btsti(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int imm=(regs>>4)&15,rd=(regs)&15;
		if (imm&8) return fail(pc-2);
		if (disassemble)	dasm(pc-2,"BTST","#%d,%s",imm,getRegName8(rd));
		if (execute)		ccrflags_set(ccr_z,!(reg8(rd)&(1<<imm)));
		return pc;
	}

	uint8 *handle_bsti(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int imm=(regs>>4)&7,rd=(regs)&15,n=(regs>>7)&1;
		if (disassemble)	dasm(pc-2,n?"BIST":"BST","#%d,%s",imm,getRegName8(rd));
		if (execute)		{int8 &r=reg8(rd);r&=~(1<<imm);r|=((ccr&1)^n)<<imm;}
		return pc;
	}
	
	uint8 *handle_bor(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int imm=(regs>>4)&7,rd=(regs)&15,n=(regs>>7)&1;
		if (disassemble)	dasm(pc-2,n?"BIOR":"BOR","#%d,%s",imm,getRegName8(rd));
		if (execute)		ccrflags_set(ccr_c,(ccr&1) | (((reg8(rd)>>imm)&1)^n));
		return pc;
	}
	uint8 *handle_bxor(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int imm=(regs>>4)&7,rd=(regs)&15,n=(regs>>7)&1;
		if (disassemble)	dasm(pc-2,n?"BIXOR":"BXOR","#%d,%s",imm,getRegName8(rd));
		if (execute)		ccrflags_set(ccr_c,(ccr&1) ^ (((reg8(rd)>>imm)&1)^n));
		return pc;
	}
	uint8 *handle_band(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int imm=(regs>>4)&7,rd=(regs)&15,n=(regs>>7)&1;
		if (disassemble)	dasm(pc-2,n?"BIAND":"BAND","#%d,%s",imm,getRegName8(rd));
		if (execute)		ccrflags_set(ccr_c,(ccr&1) & (((reg8(rd)>>imm)&1)^n));
		return pc;
	}
	uint8 *handle_bld(uint8 op,uint8 *pc)
	{
		int regs=read8(pc);pc++;	int imm=(regs>>4)&7,rd=(regs)&15,n=(regs>>7)&1;
		if (disassemble)	dasm(pc-2,n?"BILD":"BLD","#%d,%s",imm,getRegName8(rd));
		if (execute)		ccrflags_set(ccr_c, (((reg8(rd)>>imm)&1)^n));
		return pc;
	}
	
	uint8 *handle_not(uint8 op, uint8 b, uint8 *pc)
	{
		int size=((b>>4)&3)+1, r=(b&15);	// size=1,2,4
		if (size==4 && (r&8)) return fail(pc-2);
		if (disassemble)
		{
			if (size==1) dasm(pc-2,"NOT.B","%s",getRegName8(r));
			if (size==2) dasm(pc-2,"NOT.W","%s",getRegName16(r));
			if (size==4) dasm(pc-2,"NOT.L","%s",getRegName32(r));
		}
		if (execute)
		{
			if (size==1)	{reg8(r)=~reg8(r);ccrflags_val(reg8(r));}
			if (size==2)	{reg16(r)=~reg16(r);ccrflags_val(reg16(r));}
			if (size==4)	{reg32(r)=~reg32(r);ccrflags_val(reg32(r));}
		}
		return pc;
	}
	uint8 *handle_neg(uint8 op, uint8 b, uint8 *pc)
	{
		int size=((b>>4)&3)+1, r=(b&15);	// size=1,2,4
		if (size==4 && (r&8)) return fail(pc-2);
		if (disassemble)
		{
			if (size==1) dasm(pc-2,"NEG.B","%s",getRegName8(r));
			if (size==2) dasm(pc-2,"NEG.W","%s",getRegName16(r));
			if (size==4) dasm(pc-2,"NEG.L","%s",getRegName32(r));
		}
		if (execute)
		{
			if (size==1)	reg8(r)=sub8(0,reg8(r));
			if (size==2)	reg16(r)=sub16(0,reg16(r));
			if (size==4)	reg32(r)=sub32(0,reg32(r));
		}
		return pc;
	}
	uint8 *handle_extu(uint8 op, uint8 b, uint8 *pc)
	{
		int size=((b>>4)&3)+1, r=(b&15);	// size=2,4
		if (size==4 && (r&8)) return fail(pc-2);
		if (disassemble)
		{
			if (size==2) dasm(pc-2,"EXTU.W","%s",getRegName16(r));
			if (size==4) dasm(pc-2,"EXTU.L","%s",getRegName32(r));
		}
		if (execute)
		{
			if (size==2)	{reg16(r)&=0xff;	ccrflags_set(ccr_n|ccr_v,0);ccrflags_set(ccr_z,!reg16(r));}
			if (size==4)	{reg32(r)&=0xffff;	ccrflags_set(ccr_n|ccr_v,0);ccrflags_set(ccr_z,!reg32(r));}
		}
		return pc;
	}
	uint8 *handle_exts(uint8 op, uint8 b, uint8 *pc)
	{
		int size=((b>>4)&3)+1, r=(b&15);	// size=2,4
		if (size==4 && (r&8)) return fail(pc-2);
		if (disassemble)
		{
			if (size==2) dasm(pc-2,"EXTS.W","%s",getRegName16(r));
			if (size==4) dasm(pc-2,"EXTS.L","%s",getRegName32(r));
		}
		if (execute)
		{
			if (size==2)	{reg16(r)&=0xff;	if (reg16(r)&0x80)  reg16(r)|=0xff00;		ccrflags_val(reg16(r));}
			if (size==4)	{reg32(r)&=0xffff;	if (reg32(r)&0x8000)reg32(r)|=0xffff0000;	ccrflags_val(reg32(r));}
		}
		return pc;
	}
	
	uint8 *inner_bitstuff_aa(int c,int d,int aa,int type,uint8 *startpc,uint8 *pc)
	{
		int rn=(d>>4),imm=(d>>4)&7,inv=(d&0x80)?1:0;	// Only some of these are valid/used depending on op.

		if (disassemble)
		{
			if (!type)
			{
				if (inv && c==0x73)	return fail(startpc);
				switch (c)
				{
				case 0x63:	dasm(startpc,"BTST","%s,@%s",getRegName8(rn),printAddr(aa));	break;
				case 0x73:	dasm(startpc,"BTST","#%d,@%s",imm,printAddr(aa));				break;
				case 0x74:	dasm(startpc,inv?"BIOR":"BOR","#%d,@%s",imm,printAddr(aa));		break;
				case 0x75:	dasm(startpc,inv?"BIXOR":"BXOR","#%d,@%s",imm,printAddr(aa));	break;
				case 0x76:	dasm(startpc,inv?"BIAND":"BAND","#%d,@%s",imm,printAddr(aa));	break;
				case 0x77:	dasm(startpc,inv?"BILD":"BLD","#%d,@%s",imm,printAddr(aa));	break;
				default:	return fail(startpc);
				}
			}
			else
			{
				if (inv && (c==0x70 || c==0x71 || c==0x72))	return fail(startpc);
				switch (c)
				{
				case 0x60:	dasm(startpc,"BSET","%s,@%s",getRegName8(rn),printAddr(aa));	break;
				case 0x61:	dasm(startpc,"BNOT","%s,@%s",getRegName8(rn),printAddr(aa));	break;
				case 0x62:	dasm(startpc,"BCLR","%s,@%s",getRegName8(rn),printAddr(aa));	break;
				case 0x67:	dasm(startpc,inv?"BIST":"BST","#%d,@%s",imm,printAddr(aa));	break;
				case 0x70:	dasm(startpc,"BSET","#%d,@%s",imm,printAddr(aa));	break;
				case 0x71:	dasm(startpc,"BNOT","#%d,@%s",imm,printAddr(aa));	break;
				case 0x72:	dasm(startpc,"BCLR","#%d,@%s",imm,printAddr(aa));	break;
				default:	return fail(startpc);
				}
			}
		}
		if (execute)
		{
			int bi=1<<imm,rv=reg8(rn),cc=ccr&1,byte;
			if (!type)
			{
				if (inv && c==0x73)	return fail(startpc);
				byte=read8(aa);
				switch (c)
				{
				case 0x63:	ccrflags_set(ccr_z,(byte&(1<<rv))==0);			break;
				case 0x73:	ccrflags_set(ccr_z,(byte&bi)==0);				break;
				case 0x74:	ccrflags_set(ccr_c,cc|(inv^((byte>>imm)&1)));	break;
				case 0x75:	ccrflags_set(ccr_c,cc^(inv^((byte>>imm)&1)));	break;
				case 0x76:	ccrflags_set(ccr_c,cc&(inv^((byte>>imm)&1)));	break;
				case 0x77:	ccrflags_set(ccr_c,inv^((byte>>imm)&1));		break;
				default:	return fail(startpc);
				}
			}
			else
			{
				if (inv && (c==0x70 || c==0x71 || c==0x72))	return fail(startpc);
				byte=read8(aa);
				switch (c)
				{
				case 0x60:	byte|=(1<<rv);	break;
				case 0x61:	byte^=(1<<rv);	break;
				case 0x62:	byte&=~(1<<rv);	break;
				case 0x67:	byte&=~imm;byte|=(inv^cc)*imm;		break;
				case 0x70:	byte|=bi;	break;
				case 0x71:	byte^=bi;	break;
				case 0x72:	byte&=~bi;	break;
				default:	return fail(startpc);
				}
				write8(byte,aa);
			}
		}
		return pc;
	}
	
	uint8 *handle_bitstuff_1(uint8 op,uint8 *pc)	// to get here, op==0x7c, 0x7d, 0x7e, 0x7f
	{
		int b=read8(pc);pc++;
		int c=read8(pc);pc++;
		int d=read8(pc);pc++;
		if (d&0xf) return fail(pc-4);	// this MUST be clear.
		int rd=(b>>4)&7,aa=b&255,rn=(d>>4)&15,imm=(d>>4)&7,inv=(d&0x80)?1:0;	// Only some of these are valid/used depending on op.
		
		if (op==0x7e) return inner_bitstuff_aa(c,d,0xffff00|aa,0,pc-4,pc);
		if (op==0x7f) return inner_bitstuff_aa(c,d,0xffff00|aa,1,pc-4,pc);
		if (b&0x8f) return fail(pc-4);
		
		if (disassemble)
		{
			switch (op)
			{
			case 0x7c:
				if (inv && c==0x73)	return fail(pc-4);
				switch (c)
				{
				case 0x63:	dasm(pc-4,"BTST","%s,@%s",getRegName8(rn),getRegName32(rd));	break;
				case 0x73:	dasm(pc-4,"BTST","#%d,@%s",imm,getRegName32(rd));				break;
				case 0x74:	dasm(pc-4,inv?"BIOR":"BOR","#%d,@%s",imm,getRegName32(rd));		break;
				case 0x75:	dasm(pc-4,inv?"BIXOR":"BXOR","#%d,@%s",imm,getRegName32(rd));	break;
				case 0x76:	dasm(pc-4,inv?"BIAND":"BAND","#%d,@%s",imm,getRegName32(rd));	break;
				case 0x77:	dasm(pc-4,inv?"BILD":"BLD","#%d,@%s",imm,getRegName32(rd));		break;
				default:	return fail(pc-4);
				}
				break;
			case 0x7d:
				if (inv && (c==0x70 || c==0x71 || c==0x72))	return fail(pc-4);
				switch (c)
				{
				case 0x60:	dasm(pc-4,"BSET","%s,@%s",getRegName8(rn),getRegName32(rd));	break;
				case 0x61:	dasm(pc-4,"BNOT","%s,@%s",getRegName8(rn),getRegName32(rd));	break;
				case 0x62:	dasm(pc-4,"BCLR","%s,@%s",getRegName8(rn),getRegName32(rd));	break;
				case 0x67:	dasm(pc-4,inv?"BIST":"BST","#%d,@%s",imm,getRegName32(rd));		break;
				case 0x70:	dasm(pc-4,"BSET","#%d,@%s",imm,getRegName32(rd));				break;
				case 0x71:	dasm(pc-4,"BNOT","#%d,@%s",imm,getRegName32(rd));				break;
				case 0x72:	dasm(pc-4,"BCLR","#%d,@%s",imm,getRegName32(rd));				break;
				default:	return fail(pc-4);
				}
				break;
			default:	return fail(pc-4);
			}
		}
		
		if (execute)
		{
			int bi=1<<imm,rv=reg8(rn),cc=ccr&1,byte;
			switch (op)
			{
			case 0x7c:
				if (inv && c==0x73)	return fail(pc-4);
				byte=read8(reg32(rd));
				switch (c)
				{
				case 0x63:	ccrflags_set(ccr_z,(byte&(1<<rv))==0);				break;
				case 0x73:	ccrflags_set(ccr_z,(byte&bi)==0);				break;
				case 0x74:	ccrflags_set(ccr_c,cc|(inv^((byte>>imm)&1)));	break;
				case 0x75:	ccrflags_set(ccr_c,cc^(inv^((byte>>imm)&1)));	break;
				case 0x76:	ccrflags_set(ccr_c,cc&(inv^((byte>>imm)&1)));	break;
				case 0x77:	ccrflags_set(ccr_c,inv^((byte>>imm)&1));		break;
				default:	return fail(pc-4);
				}
				break;
			case 0x7d:
				if (inv && (c==0x70 || c==0x71 || c==0x72))	return fail(pc-4);
				byte=read8(reg32(rd));
				switch (c)
				{
				case 0x60:	byte|=(1<<rv);	break;
				case 0x61:	byte^=(1<<rv);	break;
				case 0x62:	byte&=~(1<<rv);	break;
				case 0x67:	byte&=~bi;byte|=(inv^cc)*bi; break;
				case 0x70:	byte|=bi;	break;
				case 0x71:	byte^=bi;	break;
				case 0x72:	byte&=~bi;	break;
				default:	return fail(pc-4);
				}
				write8(byte,reg32(rd));
				break;
			default:	return fail(pc-4);
			}
		}
		return pc;
	}
	
	uint8 *handle_bitstuff_2(uint8 op,uint8 b,uint8 *pc)	// op=6a, b=0x1? or 0x3?
	{
		uint8 *startpc=pc-2;
		int aa=0;
		if ((b&0xf0)==0x10)	{aa=se16(read16(pc));pc+=2;} else {aa=read32(pc);pc+=4;};
		int c=read8(pc);pc++;
		int d=read8(pc);pc++;
		return inner_bitstuff_aa(c,d,aa,(b&8)?1:0,startpc,pc);
	}

	//////// IMMEDIATE OPS
	uint8 *handle_cmpi(uint8 op,uint8 *pc)
	{
		int rd=op&15,imm=read8(pc);pc++;
		if (disassemble) dasm(pc-2,"CMP.B","#0x%x,%s",imm,getRegName8(rd));
		if (execute) sub8(reg8(rd),imm);
		return pc;
	}
	uint8 *handle_addi(uint8 op,uint8 *pc)
	{
		int rd=op&15,imm=read8(pc);pc++;
		if (disassemble) dasm(pc-2,"ADD.B","#0x%x,%s",imm,getRegName8(rd));
		if (execute) reg8(rd)=add8(reg8(rd),imm);
		return pc;
	}
	uint8 *handle_addxi(uint8 op,uint8 *pc)
	{
		int rd=op&15,imm=read8(pc);pc++;
		if (disassemble) dasm(pc-2,"ADDX.B","#0x%x,%s",imm,getRegName8(rd));
		if (execute) reg8(rd)=add8(reg8(rd),imm,ccr,1);
		return pc;
	}
	uint8 *handle_subxi(uint8 op,uint8 *pc)
	{
		int rd=op&15,imm=read8(pc);pc++;
		if (disassemble) dasm(pc-2,"SUBX.B","#0x%x,%s",imm,getRegName8(rd));
		if (execute) reg8(rd)=sub8(reg8(rd),imm,ccr,1);
		return pc;
	}
	uint8 *handle_ori(uint8 op,uint8 *pc)
	{
		int rd=op&15,imm=read8(pc);pc++;
		if (disassemble) dasm(pc-2,"OR.B","#0x%x,%s",imm,getRegName8(rd));
		if (execute) {int8 &r=reg8(rd);	r|=imm;	ccrflags_val(r);}
		return pc;
	}
	uint8 *handle_xori(uint8 op,uint8 *pc)
	{
		int rd=op&15,imm=read8(pc);pc++;
		if (disassemble) dasm(pc-2,"XOR.B","#0x%x,%s",imm,getRegName8(rd));
		if (execute) {int8 &r=reg8(rd);	r^=imm;	ccrflags_val(r);}
		return pc;
	}
	uint8 *handle_andi(uint8 op,uint8 *pc)
	{
		int rd=op&15,imm=read8(pc);pc++;
		if (disassemble) dasm(pc-2,"AND.B","#0x%x,%s",imm,getRegName8(rd));
		if (execute) {int8 &r=reg8(rd);	r&=imm;	ccrflags_val(r);}
		return pc;
	}
	uint8 *handle_movbi(uint8 op,uint8 *pc)
	{
		int rd=op&15,imm=read8(pc);pc++;
		if (disassemble) dasm(pc-2,"MOV.B","#0x%x,%s",imm,getRegName8(rd));
		if (execute) {int8 &r=reg8(rd);	r=imm;	ccrflags_val(r);}
		return pc;
	}

	uint8 *handle_cmpwi(uint8 op,uint8 b,uint8 *pc)
	{
		int rd=b&15,imm=read16(pc);pc+=2;
		if (disassemble) dasm(pc-4,"CMP.W","#0x%x,%s",imm,getRegName16(rd));
		if (execute) sub16(reg16(rd),imm);
		return pc;
	}
	uint8 *handle_addwi(uint8 op,uint8 b,uint8 *pc)
	{
		int rd=b&15,imm=read16(pc);pc+=2;
		if (disassemble) dasm(pc-4,"ADD.W","#0x%x,%s",imm,getRegName16(rd));
		if (execute) reg16(rd)=add16(reg16(rd),imm);
		return pc;
	}
	uint8 *handle_subwi(uint8 op,uint8 b,uint8 *pc)
	{
		int rd=b&15,imm=read16(pc);pc+=2;
		if (disassemble) dasm(pc-4,"SUB.W","#0x%x,%s",imm,getRegName16(rd));
		if (execute) reg16(rd)=sub16(reg16(rd),imm);
		return pc;
	}
	uint8 *handle_orwi(uint8 op,uint8 b,uint8 *pc)
	{
		int rd=b&15,imm=read16(pc);pc+=2;
		if (disassemble) dasm(pc-4,"OR.W","#0x%x,%s",imm,getRegName16(rd));
		if (execute) {int16 &r=reg16(rd);	r|=imm;	ccrflags_val(r);}
		return pc;
	}
	uint8 *handle_xorwi(uint8 op,uint8 b,uint8 *pc)
	{
		int rd=b&15,imm=read16(pc);pc+=2;
		if (disassemble) dasm(pc-4,"XOR.W","#0x%x,%s",imm,getRegName16(rd));
		if (execute) {int16 &r=reg16(rd);	r^=imm;	ccrflags_val(r);}
		return pc;
	}
	uint8 *handle_andwi(uint8 op,uint8 b,uint8 *pc)
	{
		int rd=b&15,imm=read16(pc)&0xffff;pc+=2;
		if (disassemble) dasm(pc-4,"AND.W","#0x%x,%s",imm,getRegName16(rd));
		if (execute) {int16 &r=reg16(rd);	r&=imm;	ccrflags_val(r);}
		return pc;
	}
	uint8 *handle_movwi(uint8 op,uint8 b,uint8 *pc)
	{
		int rd=b&15,imm=read16(pc)&0xffff;pc+=2;
		if (disassemble) dasm(pc-4,"MOV.W","#0x%x,%s",imm,getRegName16(rd));
		if (execute) {int16 &r=reg16(rd);	r=imm;	ccrflags_val(r);}
		return pc;
	}
	
	uint8 *handle_cmpli(uint8 op,uint8 b,uint8 *pc)
	{
		int rd=b&15,imm=read32(pc);pc+=4;if (rd&8) return fail(pc-6);
		if (disassemble) dasm(pc-6,"CMP.L","#0x%x,%s",imm,getRegName32(rd));
		if (execute) sub32(reg32(rd),imm);
		return pc;
	}
	uint8 *handle_addli(uint8 op,uint8 b,uint8 *pc)
	{
		int rd=b&15,imm=read32(pc);pc+=4;if (rd&8) return fail(pc-6);
		if (disassemble) dasm(pc-6,"ADD.L","#0x%x,%s",imm,getRegName32(rd));
		if (execute) reg32(rd)=add32(reg32(rd),imm);
		return pc;
	}
	uint8 *handle_subli(uint8 op,uint8 b,uint8 *pc)
	{
		int rd=b&15,imm=read32(pc);pc+=4;if (rd&8) return fail(pc-6);
		if (disassemble) dasm(pc-6,"SUB.L","#0x%x,%s",imm,getRegName32(rd));
		if (execute) reg32(rd)=sub32(reg32(rd),imm);
		return pc;
	}
	uint8 *handle_orli(uint8 op,uint8 b,uint8 *pc)
	{
		int rd=b&15,imm=read32(pc);pc+=4;if (rd&8) return fail(pc-6);
		if (disassemble) dasm(pc-6,"OR.L","#0x%x,%s",imm,getRegName32(rd));
		if (execute) {int32 &r=reg32(rd);	r|=imm;	ccrflags_val(r); }
		return pc;
	}
	uint8 *handle_xorli(uint8 op,uint8 b,uint8 *pc)
	{
		int rd=b&15,imm=read32(pc);pc+=4;if (rd&8) return fail(pc-6);
		if (disassemble) dasm(pc-6,"XOR.L","#0x%x,%s",imm,getRegName32(rd));
		if (execute) {int32 &r=reg32(rd);	r^=imm;	ccrflags_val(r); }
		return pc;
	}
	uint8 *handle_andli(uint8 op,uint8 b,uint8 *pc)
	{
		int rd=b&15,imm=read32(pc);pc+=4;if (rd&8) return fail(pc-6);
		if (disassemble) dasm(pc-6,"AND.L","#0x%x,%s",imm,getRegName32(rd));
		if (execute) {int32 &r=reg32(rd);	r&=imm;	ccrflags_val(r); }
		return pc;
	}
	uint8 *handle_movli(uint8 op,uint8 b,uint8 *pc)
	{
		int rd=b&15,imm=read32(pc);pc+=4;if (rd&8) return fail(pc-6);
		if (disassemble) dasm(pc-6,"MOV.L","#0x%08x,%s",imm,getRegName32(rd));
		if (execute) {int32 &r=reg32(rd);	r=imm;	ccrflags_val(r); }
		return pc;
	}
		
	
	uint8 *handle_daa(uint8 op,uint8 b,uint8 *pc)
	{
		int r=b&0xf;
		if (disassemble) dasm(pc-2,"DAA","%s",getRegName8(r));
		if (!execute) return pc;
		uint8 v=reg8(r);
		int l=v&0xf,u=(v>>4)&0xf,c=(ccr&ccr_c),h=(ccr&ccr_h)?1:0, toadd=0;
		if (c) {if (h) {if (u<=3) toadd=(l<=3) ? 0x66 : 0x00;} else {if (u<=2) toadd=(l<=9) ? 0x60 : 0x66;}}
		else   {if (h) {if (l<=3) toadd=(u<=9) ? 0x06 : 0x66;} else {if (l<=9) toadd=(u<=9) ? 0x00 : 0x60; else toadd=(u<=8) ? 0x06 : 0x66;}}
		reg8(r)+=toadd;
		ccrflags_val(reg8(r));
		if (toadd>6) ccrflags_set(ccr_c,1);
		return pc;
	}
	uint8 *handle_das(uint8 op,uint8 b,uint8 *pc)
	{
		int r=b&0xf;
		if (disassemble) dasm(pc-2,"DAS","%s",getRegName8(r));
		if (!execute) return pc;
		uint8 v=reg8(r);
		int l=v&0xf,u=(v>>4)&0xf,c=(ccr&ccr_c),h=(ccr&ccr_h)?1:0,toadd=0;
		if (c) {if(h) {if(u >= 6 && l >= 6) toadd = 0x9a;}	else  {if(u >= 7 && l <= 9) toadd = 0xa0;}}
		else   {if(h) {if(u <= 8 && l >= 6) toadd = 0xfa;}}
		reg8(r)=add8(reg8(r),toadd);
		return pc;
	}

	///////// CCR OPS
	uint8 *handle_stcr(uint8 op,uint8 *pc)
	{
		int rd=read8(pc);pc++;
		if ((rd>>4)>1) return fail(pc-2);
		if (disassemble) dasm(pc-2,"STC.B","%s,%s",(rd&0xf0)?"EXR":"CCR",getRegName8(rd));
		if (execute) {int8 &r=reg8(rd);	r=(rd&0xf0)?exr:ccr;}
		return pc;
	}
	uint8 *handle_ldcr(uint8 op,uint8 *pc)
	{
		int rd=read8(pc);pc++;
		if ((rd>>4)>1) return fail(pc-2);
		if (disassemble) dasm(pc-2,"LDC.B","%s,%s",getRegName8(rd),(rd&0xf0)?"EXR":"CCR");
		if (execute) {const int8 &r=reg8(rd);if (rd&0xf0) exr=r; else ccr=r; }
		return pc;
	}
	uint8 *handle_orc(uint8 op,uint8 *pc)
	{
		int imm=read8(pc);pc++;
		if (disassemble) dasm(pc-2,"ORC","#0x%02X,CCR",imm&255);
		if (execute) ccr|=imm;
		return pc;
	}
	uint8 *handle_xorc(uint8 op,uint8 *pc)
	{
		int imm=read8(pc);pc++;
		if (disassemble) dasm(pc-2,"XORC","#0x%02X,CCR",imm&255);
		if (execute) ccr^=imm;
		return pc;
	}
	uint8 *handle_andc(uint8 op,uint8 *pc)
	{
		int imm=read8(pc);pc++;
		if (disassemble) dasm(pc-2,"ANDC","#0x%02X,CCR",imm&255);
		if (execute) ccr&=imm;
		return pc;
	}
	uint8 *handle_ldci(uint8 op,uint8 *pc)
	{
		int imm=read8(pc);pc++;
		if (disassemble) dasm(pc-2,"LDC","#0x%02X,CCR",imm&255);
		if (execute) ccr=imm;
		return pc;
	}
	
	//////// BCC
	uint8 *inner_bcc(uint8 *startpc,uint8 *pc,int cc,int imm)
	{
		const char *ccs[16]={"BRA","BRN","BHI","BLS","BCC","BCS","BNE","BEQ","BVC","BVS","BPL","BMI","BGE","BLT","BGT","BLE"};
		if (disassemble) dasm(startpc,ccs[cc],"%s",printAddr(addr(pc+imm)));

		if (!execute) return pc;
		// ALL tests are in pairs, by should the results be zero or 1 The even numbered case should be 0, the odd ones should be 1.
		int cc2=cc>>1,test=0;
		switch (cc2&7)
		{
		case 0:	test=0;										break;	//  A / N
		case 1:	test=ccr&(ccr_c|ccr_z);						break;	// HI / LS
		case 2:	test=ccr&ccr_c;								break;	// CC / CS
		case 3: test=ccr&ccr_z;								break;	// NE / EQ
		case 4:	test=ccr&ccr_v;								break;	// VC / VS
		case 5:	test=ccr&ccr_n;								break;	// PL / MI
		case 6:	test=((ccr>>2)^ccr)&ccr_v;					break;	// GE / LT
		case 7:	test=(((ccr>>2)^ccr)&ccr_v)|(ccr&ccr_z);	break;	// GT / LE
		}
		test=(test)?1:0;	// force bits down to 1bit.
		if (test==(cc&1)) pc+=imm;	// if it matches, we're set.
		return pc;
	}
	uint8 *handle_bcc(uint8 op,uint8 *pc)
	{
		int cc=op&15,imm=read8(pc);pc++;
		return inner_bcc(pc-2,pc,cc,imm);
	}
	uint8 *handle_bccw(uint8 op,uint8 *pc)
	{
		int b=read8(pc);pc++;
		if (b&0xf) return fail(pc-2);
		int cc=(b>>4)&15,imm=se16(read16(pc));pc+=2;
		if (execute) clockI(2);
		return inner_bcc(pc-4,pc,cc,imm);
	}

	
	uint8 *handle_rts(uint8 op,uint8 *pc)
	{
		int test=read8(pc);pc++;
		if (test!=0x70) return fail(pc-2);
		if (disassemble) dasm(pc-2,"RTS","");
		if (execute) {indent--; clockI(2);return popPC();}
		return pc;
	}

	uint8 *handle_rte(uint8 op,uint8 *pc)
	{
		int test=read8(pc);pc++;
		if (test!=0x70) return fail(pc-2);
		if (disassemble) dasm(pc-2,"RTE","");
		if (execute) {indent--; clockI(2); pc = popPC(true);}
		return pc;
	}
	
	uint8 *handle_bsr(uint8 op,uint8 *pc)
	{
		int imm=read8(pc);pc++;
		if (disassemble) dasm(pc-2,"BSR","%s",printAddr(addr(pc+imm)));
		if (!execute) return pc;
		pushPC(pc);
		indent++;
		return pc+imm;
	}

	uint8 *handle_bsrw(uint8 op,uint8 *pc)
	{
		int imm=read8(pc);pc++;
		if (imm) return fail(pc-2);
		imm=se16(read16(pc));pc+=2;
		if (disassemble) dasm(pc-4,"BSR","%s",printAddr(addr(pc+imm)));
		if (!execute) return pc;
		pushPC(pc);
		indent++;
		clockI(2);
		return pc+imm;
	}
	
	uint8 *handle_trapa(uint8 op,uint8 *pc)
	{
		int imm=read8(pc);pc++;
		if (imm&0xCF) return fail(pc-2);
		if (disassemble) dasm(pc-2,"TRAPA","%d",imm>>4);
		if (!execute) return pc;
		pushPC(pc,true);
		ccrflags_set(ccr_i, 1);
		bool ue = read8(0xfffff2) & 8;
		if (!ue) ccrflags_set(ccr_u, 1);
		indent++;
		clockI(4);
		return loadPCFrom(32+(imm>>2));	// Offset for TRAP calls is 0x20 + 4 * trap number.
	}
	
	uint8 *handle_jmpae(uint8 op,uint8 *pc)
	{
		int er=read8(pc);pc++;
		if (er&0x8f) return fail(pc-2);
		if (disassemble) dasm(pc-2,"JMP","@%s",getRegName32(er>>4));
		if (execute) return (reg32(er>>4)&0xffffff)+memory;
		return pc;
	}
	uint8 *handle_jmpimm(uint8 op,uint8 *pc)
	{
		int addr=(read16(pc) << 8) | (read8(pc+2) & 0xff);pc+=3;
		if (disassemble) dasm(pc-4,"JMP","%s",printAddr(addr));
		if (execute) {clockI(2); return memory+addr;}
		return pc;
	}
	uint8 *handle_jmpaa(uint8 op,uint8 *pc)
	{
		int imm=(read8(pc))&255;pc++;
		if (disassemble) dasm(pc-2,"JMP","@@0x%02x",imm);
		if (!execute) return pc;
		clockI(2);
		return loadPCFrom(imm&~1);	// MASK THIS OFF! It's valid, but ignored.
	}

	uint8 *handle_jsrae(uint8 op,uint8 *pc)
	{
		int er=read8(pc);pc++;
		if (er&0x8f) return fail(pc-2);
		if (disassemble) dasm(pc-2,"JSR","@%s",getRegName32(er>>4));
		if (!execute) return pc;
		pushPC(pc);
		indent++;
		return (reg32(er>>4)&0xffffff)+memory;
	}
	uint8 *handle_jsrimm(uint8 op,uint8 *pc)
	{
		int addr=(read16(pc) << 8) | (read8(pc+2) & 0xff);pc+=3;
		if (disassemble) dasm(pc-4,"JSR","%s",printAddr(addr));
		if (!execute) return pc;
		pushPC(pc);
		indent++;
		clockI(2);
		return memory+addr;
	}
	uint8 *handle_jsraa(uint8 op,uint8 *pc)
	{
		int imm=(read8(pc))&255;pc++;
		if (disassemble) dasm(pc-2,"JSR","@@0x%02x",imm);
		if (!execute) return pc;
		pushPC(pc);
		indent++;
		return loadPCFrom(imm&~1);	// MASK THIS OFF! It's valid, but ignored.
	}

	
	uint8 *handle_eepmov(uint8 op,uint8 *pc)
	{
		int r=read8(pc);pc++;int w=0;
		if (r==0x5c) w=0; else if (r==0xd4) w=1; else return fail(pc-2);
		if (read8(pc)!=0x59 || (read8(pc+1)& 0xff)!=0x8f) return fail(pc-2); pc+=2;
		if (disassemble) dasm(pc-4,w?"EEPMOV.W":"EEPMOV.B","");
		if (!execute) return pc;
		int32 &e5=reg32(5),&e6=reg32(6);
		if (w)
		{
			int16 &r4=reg16(4);
			while (r4!=0) {	write8(read8(e5),e6);e5++;e6++;r4--;}
		}
		else
		{
			int8 &r4l=reg8(8+4);
			while (r4l!=0) {	write8(read8(e5),e6);e5++;e6++;r4l--;}
		}
		return pc;
	}
	
	//////// NOP
	uint8 *handle_nop(uint8 op,uint8 *pc)
	{
		int b=read8(pc);pc++;
		if (b) return fail(pc-2);
		if (disassemble) dasm(pc-2,"NOP","");
		return pc;
	}
	
	uint8 *handle_sleep(uint8 op,uint8 b,uint8 *pc)
	{
		if (disassemble) dasm(pc-2,"SLEEP","");
		if (execute) pc-=2;
		return pc;
	}

	//////// END OF OPS
	void dasm(uint8 *pc,const char *op,const char *params,...)
	{
		if(!g_dasm)
			return;

		printf("%06x:\t%s\t",addr(pc),op);
		va_list args;
		va_start(args, params);
		vprintf(params, args);
		printf("\n");
	}
	int indent {0};

	int8 add8(int value1,int value2,int c = 0,bool keepZ=false)
	{
		value1 = se8(value1);
		value2 = se8(value2);
		c&=1;
		int res=value1+value2 + c;
		if (res & 0xff) ccrflags_set(ccr_z,0);
		else if (!keepZ) ccrflags_set(ccr_z,1);
		ccrflags_set(ccr_n,res & 0x80);
		ccrflags_set(ccr_c,((value1 & value2) | (value1 & ~res) | (value2 & ~res)) & 0x80);
		ccrflags_set(ccr_h,((value1 & value2) | (value1 & ~res) | (value2 & ~res)) & 0x8);
		ccrflags_set(ccr_v,((value1 & value2 & ~res) | (~value1 & ~value2 & res)) & 0x80);
		return res;
	}

	int16 add16(int value1,int value2)
	{
		value1 = se16(value1);
		value2 = se16(value2);
		int res=value1+value2;
		ccrflags_set(ccr_z,(res & 0xffff) ? 0 : 1);
		ccrflags_set(ccr_n,res & 0x8000);
		ccrflags_set(ccr_c,((value1 & value2) | (value1 & ~res) | (value2 & ~res)) & 0x8000);
		ccrflags_set(ccr_h,((value1 & value2) | (value1 & ~res) | (value2 & ~res)) & 0x800);
		ccrflags_set(ccr_v,((value1 & value2 & ~res) | (~value1 & ~value2 & res)) & 0x8000);
		return res;
	}

	int32 add32(int value1,int value2,bool noccr=false)
	{
		int res=(int)((int64_t)value1+ value2) & 0xffffffff;	// allow overflow.
		if (noccr) return res;
		ccrflags_set(ccr_z,res ? 0 : 1);
		ccrflags_set(ccr_n,res & 0x80000000);
		ccrflags_set(ccr_c,((value1 & value2) | (value1 & ~res) | (value2 & ~res)) & 0x80000000);
		ccrflags_set(ccr_h,((value1 & value2) | (value1 & ~res) | (value2 & ~res)) & 0x8000000);
		ccrflags_set(ccr_v,((value1 & value2 & ~res) | (~value1 & ~value2 & res)) & 0x80000000);
		return res;
	}

	int8 sub8(int value1,int value2, int c = 0,bool keepZ=false)
	{
		value1 = se8(value1);
		value2 = se8(value2);
		c&=1;
		int res=value1 - value2 - c;
		if (res & 0xff) ccrflags_set(ccr_z,0);
		else if (!keepZ) ccrflags_set(ccr_z,1);
		ccrflags_set(ccr_n,res & 0x80);
		ccrflags_set(ccr_c,((~value1 & value2) | (~value1 & res) | (value2 & res)) & 0x80);
		ccrflags_set(ccr_h,((~value1 & value2) | (~value1 & res) | (value2 & res)) & 0x8);
		ccrflags_set(ccr_v,((value1 & ~value2 & ~res) | (~value1 & value2 & res)) & 0x80);
		return res;
	}
	
	int16 sub16(int value1,int value2)
	{
		value1 = se16(value1);
		value2 = se16(value2);
		int res=value1-value2;
		ccrflags_set(ccr_z,(res & 0xffff) ? 0 : 1);
		ccrflags_set(ccr_n,res & 0x8000);
		ccrflags_set(ccr_c,((~value1 & value2) | (~value1 & res) | (value2 & res)) & 0x8000);
		ccrflags_set(ccr_h,((~value1 & value2) | (~value1 & res) | (value2 & res)) & 0x800);
		ccrflags_set(ccr_v,((value1 & ~value2 & ~res) | (~value1 & value2 & res)) & 0x8000);
		return res;
	}
	
	int32 sub32(int value1,int value2,bool noccr=false)
	{
		int res=value1-value2;
		if (noccr) return res;
		ccrflags_set(ccr_z,res ? 0 : 1);
		ccrflags_set(ccr_n,res & 0x80000000);
		ccrflags_set(ccr_c,((~value1 & value2) | (~value1 & res) | (value2 & res)) & 0x80000000);
		ccrflags_set(ccr_h,((~value1 & value2) | (~value1 & res) | (value2 & res)) & 0x8000000);
		ccrflags_set(ccr_v,((value1 & ~value2 & ~res) | (~value1 & value2 & res)) & 0x80000000);
		return res;
	}
};

typedef H8S<true,false> H8SEmulator;
typedef H8S<false,true> H8SDisassembler;
typedef H8S<true,true> H8SLoggingEmulator;
