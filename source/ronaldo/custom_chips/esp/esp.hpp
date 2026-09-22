#pragma once

#include <stdint.h>
#include <stdio.h>
#include <algorithm>
#include <assert.h>
#include <array>

template <int32_t N> static constexpr int32_t se(int32_t x) { x <<= (32 - N); return x >> (32 - N); }

template<int lg2eram_size>
class ESP;

template<int lg2eram_size>
class ESPCore;

template<int lg2eram_size>
class ERAM;

#include "esp_opt.hpp"


class DspAccumulator {
public:
	inline void reset() {for (int i = 0; i < delay; i++) hist[i] = 0; head = acc = 0;}
	inline int32_t operator=(int32_t v) { return acc = se<30>(v); }
	inline int32_t operator+=(int32_t v) { return acc = se<30>(acc + v); }
	
	inline int32_t rawFull() const { return acc; }
	inline int32_t getPipelineSat24() const { return std::clamp(hist[head], -0x800000, 0x7fffff); }
	inline int32_t getPipelineRaw24() const { return se<24>(hist[head]); }
	inline int32_t getPipelineRawFull() const { return hist[head]; }
	
	inline void storePipeline() { hist[head++] = acc; if (head == delay) head = 0; }
protected:
	static constexpr int delay {3}; // delay
	int32_t acc {0}, hist[delay] = {}, head {0};
};

// NOTE: we assume it's being used the 16 bit compressed mode
template<int lg2eram_size>
class ERAM {
public:
	friend class ESPOptimizer<lg2eram_size>;

	void reset() {
		if (eram_size) memset(eram, 0, eram_size * sizeof(int32_t));
		eramPos = eramEffectiveAddr = eramImmOffsetAccNext = eramVarOffset = eramWriteLatch = eramPCCommit = eramPCStartNext = eramModeCurrent = eramModeNext = 0;
		eramActiveCurrent = eramActiveNext = false;
	}
	
	void tickSample() { eramPos = (eramPos - 1) & ERAM_MASK_FULL; }
	
	void tickCycle(const uint8_t eramCtrl, const uint16_t pc) {
		int stage1 = pc - eramPCStartNext;

		// Transaction start
		if (!eramActiveNext && ((eramCtrl & 0x18) != 0)) {
			eramActiveNext = true;
			eramModeNext = eramCtrl;
			eramPCStartNext = pc;
			eramImmOffsetAccNext = 0;
			stage1 = 0;
			if (eramModeNext & 0x7) printf("wtf %03x at pc=%04x\n", eramCtrl, pc);
		}

		// Accumulate immediates
		else if (eramActiveNext && stage1 <= 4 && stage1 > 0) {
			eramImmOffsetAccNext += eramCtrl << ((stage1 - 1) * 5);
		}

		// Is it time to commit?
		if (eramActiveCurrent && (pc == eramPCCommit)) {
			if (eramModeCurrent == 0x10) eram[eramEffectiveAddr & ERAM_MASK] = crunch(eramWriteLatchNext);	// write
			else eramReadLatch = se<24>(eram[eramEffectiveAddr & ERAM_MASK]); // read
			eramActiveCurrent = false; // done
		}

		// Next stage
		if (eramActiveNext && stage1 == 5) { // FIXME: stage1 should be 4, but there are some problems with latching
			if (eramActiveCurrent) printf("ERAM transaction already active at pc %03x\n", pc);
			eramActiveCurrent = true;
			eramModeCurrent = eramModeNext;
			eramPCCommit = eramPCStartNext + ERAM_COMMIT_STAGE;
			eramActiveNext = false;
			eramWriteLatchNext = eramWriteLatch;

			// Addr computation
			eramEffectiveAddr = eramPos + eramImmOffsetAccNext;
			if (eramModeNext == 0x18)
			{
				eramEffectiveAddr = eramPos + (eramVarOffset >> 12) + ((eramImmOffsetAccNext >> 1) & 1);
				eramHighOffset = eramImmOffsetAccNext & 0x100;
			}
			// HACK for now
			if (eramHighOffset && eramPos <= 0x4000) eramEffectiveAddr += 0x40000;
			if (eramHighOffset && eramPos  > 0x4000) eramEffectiveAddr += 0xc0000;
		}
	}

	int32_t eramReadLatch = 0, eramWriteLatch = 0, eramVarOffset = 0;
protected:
	static constexpr int64_t ERAM_COMMIT_STAGE = 10, ERAM_MASK_FULL = (1 << 19) - 1;
	enum {eram_size = 1 << lg2eram_size, ERAM_MASK = eram_size - 1};
	int32_t eram[eram_size];
	uint32_t eramPos = 0, eramEffectiveAddr = 0, eramImmOffsetAccNext = 0, eramHighOffset = 0;
	int32_t eramWriteLatchNext = 0;
	uint16_t eramPCCommit = 0, eramPCStartNext = 0;
	uint8_t eramModeCurrent = 0, eramModeNext = 0;
	bool eramActiveCurrent = false, eramActiveNext = false;

	// Accurate implementation of the ESP ERAM compression behavior
	inline static int crunch(int x) {
		const int b = ((x >> 1) & 0x400000) * 3;
		if (((x << 1) & 0xc00000) != b) return x & 0xFFFFFC00;
		if (((x << 3) & 0xc00000) != b) return x & 0xFFFFFF00;
		if (((x << 5) & 0xc00000) != b) return x & 0xFFFFFFC0;
		return x & 0xFFFFFFF0;
	}
};

template<int lg2eram_size>
struct SharedState {
	int32_t gram[256] {};
	uint8_t readback_regs[4] = {0xff, 0xff, 0xff, 0x00};
	int32_t mulcoeffs[8] = {};
	ERAM<lg2eram_size> eram;

	void reset() {
		memset(gram, 0, sizeof(gram));
		memset(readback_regs, 0x00, sizeof(readback_regs));
		memset(mulcoeffs, 0, sizeof(mulcoeffs));
		eram.reset();
	}
};

template<int lg2eram_size>
class ESPCore
{
public:
	friend class ESPOptimizer<lg2eram_size>;

	void reset() { memset(iram, 0, sizeof(iram)); pc = iramPos = 0; accA.reset(); accB.reset(); }

	void setup(const uint32_t *_pram, SharedState<lg2eram_size> *_shared) { pram = _pram; shared = _shared; }

	void writeGRAM(int32_t val, uint32_t offset) {shared->gram[(offset + iramPos) & IRAM_MASK] = val;}
	int32_t readGRAM(uint32_t offset) const {return shared->gram[(offset + iramPos) & IRAM_MASK];}

	void writeIRAM(int32_t val, uint32_t offset) { iram[(offset + iramPos) & IRAM_MASK] = val; }
	int32_t readIRAM(uint32_t offset) const {return iram[(offset + iramPos) & IRAM_MASK];}

	void sync() {
		pc = 0;
		iramPos = (iramPos - 1) & IRAM_MASK;
	}
	
	void steperam() { if (lg2eram_size) shared->eram.tickCycle((pram[pc] >> 23) & 0x1f, pc); }

	void step() {
		if (pc >= PRAM_SIZE) return;
		
		if (pc == pcjumpat) {pc = pcjumpto; pcjumpat = -1;}

		// Decode instr
		const uint32_t instr = pram[pc++]; // PC advances here.
		if (!instr) {
			skipfield >>= 1;
			accA.storePipeline();
			accB.storePipeline();
			return;
		}
		const uint8_t op = (instr >> 16) & 0x7c;
		const uint8_t mem = (instr >> 10) & 0xff;
		const uint8_t shiftbits = (instr >> 8) & 3;
		uint8_t shift = (0x3567 >> (shiftbits << 2)) & 0xf; // this is shift amount. pick the value 3/5/6/7 using bits 8,9.
		const uint8_t coef = instr & 0xff;

		const uint32_t mempos = ((uint32_t)mem + iramPos) & IRAM_MASK;
		int32_t mulInputA_24 = 0;
		switch (mem)
		{
			case 1: mulInputA_24 = 0x10; break; // 4
			case 2: mulInputA_24 = 0x400; break; // 10
			case 3: mulInputA_24 = 0x10000; break; // 16
			case 4: mulInputA_24 = 0x400000; break; // 22
			default: mulInputA_24 = iram[mempos]; break;
		}
		int32_t mulInputB_24 = coef;
		bool acc = false, clr = false;
		bool setcondition = false;
		switch (op)
		{
			// MAC
			case 0x00: break;
			case 0x04: clr = true; break;
			case 0x08: iram[mempos] = mulInputA_24 = accA.getPipelineSat24(); clr = true; break;
			case 0x0c: iram[mempos] = mulInputA_24 = accB.getPipelineSat24(); clr = true; break;
			case 0x10: acc = true; break;
			case 0x14: acc = true; clr = true; break;
			case 0x18: acc = true; iram[mempos] = mulInputA_24 = accA.getPipelineSat24(); clr = true; break;
			case 0x1c: acc = true; iram[mempos] = mulInputA_24 = accB.getPipelineSat24(); clr = true; break;
			
			// SPECIAL/MUL/GRAM
			case 0x20:
				acc = (shiftbits & 2);
				shift = (shiftbits & 1) ? 6 : 7;
				mulInputA_24 = shared->gram[mempos];
				break;
			case 0x24:
				acc = (shiftbits & 2);
				clr = true;
				shift = (shiftbits & 1) ? 6 : 7;
				mulInputA_24 = shared->gram[mempos];
				break;
			case 0x28: printf("Unexpected Opcode 0x28. This should be unused\n"); break;
			case 0x2c: printf("Unexpected Opcode 0x2c. This should be unused\n"); break;
			case 0x30:
			{
				acc = (coef & 2);
				clr = !(coef & 1);
				bool weird = (coef & 0x1c) == 0x1c;
				if (coef & 4) {
					mulInputA_24 = (acc ? accB : accA).getPipelineSat24();
					if (weird) mulInputA_24 = (mulInputA_24 >= 0) ? 0x7fffff : 0xFF800000;
					iram[mempos] = mulInputA_24;
				}
				mulInputB_24 = shared->mulcoeffs[coef >> 5];
				if ((coef >> 5) == 6) mulInputB_24 = (shared->eram.eramVarOffset << 11) & 0x7fffff;
				if ((coef >> 5) == 7) mulInputB_24 = shared->mulcoeffs[5];
				if ((coef & 8) && !weird) mulInputB_24 *= -1;
				if ((coef & 16) && mulInputB_24 >= 0 && !weird) mulInputB_24 = (~mulInputB_24 & 0x7fffff);
				else if ((coef & 16) && mulInputB_24 < 0 && !weird) mulInputB_24 = ~(mulInputB_24 & 0x7fffff);
				last_mulInputB_24 = mulInputB_24;
				mulInputB_24 >>= 16;
			}
				break;
			case 0x34:
				if (mem < 0xa0 || (mem & 0xf0) == 0xb0) printf("Unexpected value for mem (%02x) with opcode 0x34\n", mem);
				if (mem >= 0xa0 && mem < 0xb0) shared->mulcoeffs[(mem >> 1) & 7] = ((mem & 1) ? accB : accA).getPipelineSat24();
				if (mem >= 0xc0)
				{
					acc = (mem & 0x20);
					clr = (mem & 0x10);
					DspAccumulator &ac = (acc) ? accB : accA;
					switch (mem & 0xf)
					{
						case 0x0: if (!ac.getPipelineRawFull()) jumpto(coef); break;
						case 0x1: if (ac.getPipelineRawFull() < 0) jumpto(coef); break;
						case 0x2: if (ac.getPipelineRawFull() > 0) jumpto(coef); break;
						case 0x3: jumpto(coef); break;
						case 0x4: /* set INT pins */ break;
						case 0x6:
							// double precision
							mulInputA_24 = last_mulInputA_24 >> 7;
							if (lastMul30) mulInputB_24 = (last_mulInputB_24 >> 9) & 0x7f;
							break;
						case 0x7: shared->eram.eramVarOffset = ac.getPipelineRawFull(); break;
						case 0xa: *((int32_t*)&shared->readback_regs) = ac.getPipelineSat24(); break;
						case 0xb: shared->eram.eramWriteLatch = ac.getPipelineSat24(); break;
						case 0xc:
						case 0xd:
						case 0xe:
						case 0xf:
							mulInputA_24 = shared->eram.eramReadLatch;
							writeIRAM(mulInputA_24, mem | 0xf0);
							break;
						default:
							printf("Unknown value for mem (%02x) with opcode 0x34\n", mem);
							break;
					}
				}
				break;
			case 0x38: shared->gram[mempos] = mulInputA_24 = accA.getPipelineSat24(); break;
			case 0x3c: shared->gram[mempos] = mulInputA_24 = accB.getPipelineSat24(); break;
			
			// UNSAT/CLAMP
			case 0x40: iram[mempos] = mulInputA_24 = accA.getPipelineRaw24(); break;
			case 0x44: iram[mempos] = mulInputA_24 = accA.getPipelineRaw24(); clr = true; break;
			case 0x48: iram[mempos] = mulInputA_24 = std::max(0, accA.getPipelineSat24()); break;
			case 0x4c: iram[mempos] = mulInputA_24 = std::max(0, accA.getPipelineSat24()); clr = true; break;
			
			case 0x50:
				// Super unsure here! The hardware seems to be clearing the acc, but it works better without
				clr = true;
				setcondition = true;
				break;
			case 0x54: printf("Mysterious opcode 54 at pc = %04x\n", pc - 1); break; // TODO: what is this?
			case 0x58: iram[mempos] = mulInputA_24 = accA.getPipelineSat24(); break;
			case 0x5c: acc = true; iram[mempos] = mulInputA_24 = accB.getPipelineSat24(); break;
			
			// TODO: mask 0x7fffff might be different, but the important thing is to remove the sign
			case 0x60: mulInputA_24 = (~mulInputA_24 & 0x7fffff); break;
			case 0x64: clr = true; mulInputA_24 = (~mulInputA_24 & 0x7fffff); break;
			case 0x68:
				iram[mempos] = mulInputA_24 = accA.getPipelineSat24();
				if (mulInputA_24 >= 0) mulInputA_24 = ~mulInputA_24;
				mulInputA_24 &= 0x7fffff;
				break;
			case 0x6c:
				iram[mempos] = mulInputA_24 = accA.getPipelineSat24();
				if (mulInputA_24 >= 0) mulInputA_24 = ~mulInputA_24;
				mulInputA_24 &= 0x7fffff;
				clr = true;
				break;
			case 0x70: mulInputA_24 = (~mulInputA_24 & 0x7fffff); break;
			case 0x74: clr = true; mulInputA_24 = (~mulInputA_24 & 0x7fffff); break;
			case 0x78:
				iram[mempos] = mulInputA_24 = accA.getPipelineSat24();
				if (mulInputA_24 < 0) mulInputA_24 = ~mulInputA_24;
				mulInputA_24 &= 0x7fffff;
				break;
			case 0x7c:
				iram[mempos] = mulInputA_24 = accA.getPipelineSat24();
				if (mulInputA_24 < 0) mulInputA_24 = ~mulInputA_24;
				mulInputA_24 &= 0x7fffff;
				clr = true;
				break;
			
			default: printf("mysterious\n"); break; // TODO: few more opcodes here
		}
		
		if (skipfield & 1) mulInputA_24 = 0;

		// Multiplier
		DspAccumulator &storeAcc = (acc) ? accB : accA;
		if (clr) storeAcc = 0;
		int64_t mulResult = (int64_t)se<24>(mulInputA_24) * (int64_t) se<8>(mulInputB_24);
		mulResult >>= shift;
		storeAcc += (int32_t)mulResult;

		skipfield >>= 1;
		if (setcondition) skipfield |= (storeAcc.rawFull() < 0) ? 0x3c0 : 0x30;

		last_mulInputA_24 = mulInputA_24;
		lastMul30 = (op == 0x30);
		accA.storePipeline();
		accB.storePipeline();
	}
protected:
	static constexpr int64_t PRAM_SIZE = 768, IRAM_SIZE = 0x100, IRAM_MASK = IRAM_SIZE - 1;
	void jumpto(uint16_t newpc) { if (pcjumpat != -1) printf("Oh no! Jump overlap!\n"); pcjumpto = newpc; pcjumpat = pc + 2;}

	int32_t iram[IRAM_SIZE] {}, last_mulInputA_24 {0}, last_mulInputB_24 {0}, skipfield {0};
	bool lastMul30 = false;
	const uint32_t *pram {nullptr};
	uint32_t pc = 0, iramPos = 0;
	int pcjumpat {-1}, pcjumpto {-1};
	DspAccumulator accA, accB;
	SharedState<lg2eram_size> *shared;
};


template<int lg2eram_size>
class ESP
{
public:
	friend class ESPOptimizer<lg2eram_size>;
	ESPOptimizer<lg2eram_size> opt;

	ESP() : opt(this) {
		core0.setup((uint32_t*)&intmem[0x0000], &shared);
		core1.setup((uint32_t*)&intmem[0x1000], &shared);
		reset();
	}

	void reset() {
		memset(intmem, 0, sizeof(intmem));
		core0.reset();
		core1.reset();
		shared.reset();
	}

	// Interface with hardware / other chips etc.
	void writeGRAM(int32_t val, uint8_t offset) {core0.writeGRAM(val, offset);}
	int32_t readGRAM(uint8_t offset) const {return core0.readGRAM(offset);}
	int32_t readIRAM0(uint8_t offset) const {return core0.readIRAM(offset);}
	int32_t readIRAM1(uint8_t offset) const {return core1.readIRAM(offset);}
	void writeIRAM0(int32_t val, uint8_t offset) {core0.writeIRAM(val, offset);}
	void writeIRAM1(int32_t val, uint8_t offset) {core1.writeIRAM(val, offset);}
	uint8_t readPRAM(size_t offset) const {return intmem[offset];}

	// For running apart from h8 emu
	void writePMem(uint16_t address, uint8_t val) {intmem[address] = val;}
	void writePMem32(uint16_t address, uint32_t val, bool _recompile = true) {((uint32_t*)&intmem[0])[address] = val;} // addresses are /4 here
	uint32_t readHostReg() const {return *(uint32_t *)&shared.readback_regs[0];}
	void step_cores() { core1.steperam(); core1.step(); core0.step();}
	void sync_cores() {core0.sync(); core1.sync(); if (lg2eram_size) shared.eram.tickSample();}

	uint8_t readuC(uint32_t address) { return shared.readback_regs[address & 3]; }

	void writeuC(uint32_t address, uint8_t value) {
		address&=0x3fff;
		program_writing_word[address & 3] = value;

		if (address == 0x2003) {
			if_mode = value;
		}
		else if (if_mode == 0x54 && (address & 3) == 3) {
			const int addr = address >> 2;

			auto oldValue = *reinterpret_cast<uint32_t*>(&intmem[(addr << 2)]);

			intmem[(addr<<2) + 0] = program_writing_word[0];
			intmem[(addr<<2) + 1] = program_writing_word[1];
			intmem[(addr<<2) + 2] = program_writing_word[2];
			intmem[(addr<<2) + 3] = program_writing_word[3];

			auto newValue = *reinterpret_cast<uint32_t*>(&intmem[(addr << 2)]);

			if (newValue != oldValue)
				opt.setProgramDirty();
		}
		else if (if_mode == 0x55 && (address & 3) == 3) {
			const int addr = address >> 2;
			uint32_t *pmem = (uint32_t*)intmem;
			
			pmem[addr] &= 0xffffff00;
			pmem[addr] |= program_writing_word[0] & 0xff;
			pmem[addr] &= 0xfffffcff;
			pmem[addr] |= (program_writing_word[1] & 3) << 8;
			pmem[addr + 1] &= 0xffffffc0;
			pmem[addr + 1] |= (program_writing_word[1] >> 2) & 0x3f;
			pmem[addr + 1] &= 0xfffffc3f;
			pmem[addr + 1] |= (program_writing_word[2] & 0xf) << 6;

			// opt.genProgram(this);
			opt.updateCoef(this);
		}
		else if (if_mode == 0x56 && (address & 3) == 3) {
			const int addr = address >> 2;
			uint32_t *pmem = (uint32_t*)intmem;

			const std::array<uint32_t ,5> oldValues{
				pmem[addr],
				pmem[addr + 1],
				pmem[addr + 2],
				pmem[addr + 3],
				pmem[addr + 4]
			};
			
			pmem[addr] &= 0xf07fffff;
			pmem[addr] |= ((program_writing_word[0] >> 3) & 0x1f) << 23;
			pmem[addr + 1] &= 0xf07fffff;
			pmem[addr + 1] |= (program_writing_word[1] & 0x1f) << 23;
			pmem[addr + 2] &= 0xfc7fffff;
			pmem[addr + 2] |= ((program_writing_word[1] >> 5) & 7) << 23;
			pmem[addr + 2] &= 0xf3ffffff;
			pmem[addr + 2] |= (program_writing_word[2] & 3) << 26;
			pmem[addr + 3] &= 0xf07fffff;
			pmem[addr + 3] |= ((program_writing_word[2] >> 2) & 0x1f) << 23;
			pmem[addr + 4] &= 0xff7fffff;
			pmem[addr + 4] |= ((program_writing_word[2] >> 7) & 1) << 23;
			pmem[addr + 4] &= 0xf0ffffff;
			pmem[addr + 4] |= (program_writing_word[3] & 0xf) << 24;

			const std::array<uint32_t, 5> newValues{
				pmem[addr],
				pmem[addr + 1],
				pmem[addr + 2],
				pmem[addr + 3],
				pmem[addr + 4]
			};

			if (newValues != oldValues)
				opt.setProgramDirty();
		}
		else if (if_mode == 0x57 && (address & 3) == 3) {
			addr_sel = address & ~3;
			shared.readback_regs[0] = intmem[addr_sel+0];
			shared.readback_regs[1] = intmem[addr_sel+1];
			shared.readback_regs[2] = intmem[addr_sel+2];
			shared.readback_regs[3] = intmem[addr_sel+3];
			// printf("Asic sel addr 0x%04x\n",addr_sel);
		}
		else {
			// printf("Asic unknown %x write 0x%06x, 0x%02x\n",if_mode,address, value&255);
		}
	}

	// Debug
	void dump(const char *path_bin = "dumps/asic.bin", const char *path_txt = "dumps/asic.txt") {
		FILE *f = fopen(path_bin, "wb"); fwrite(intmem, 0x4000, 1, f); fclose(f);
		dasm_all(path_txt);
	}
	
	void transpile(const char *mode) {
		FILE *f = fopen("dumps/whatever.txt", mode);
		fprintf(f, "////////////////////////////////\n");
		transpile(f, false);
		fprintf(f, "\n\n");
		transpile(f, true);
		fprintf(f, "////////////////////////////////\n\n\n\n");
		fclose(f);
	}

	void disassembleFirmware(const char *filename, const char *mode) {
		FILE *f = fopen(filename, "rb"); fread(intmem, 0xc00, 1, f); fread(intmem + 0x1000, 0xc00, 1, f); fclose(f);
		dasm_all("dumps/asic_disassemble.txt", mode);
	}

protected:
	static constexpr int32_t clockrate = 768 * 2 * 44100; // This is clockrate. It MAY NOT be fs in that last term. DO NOT substitute fs in there.
	const int32_t fs {44100}; // samplerate.
	const int32_t stepsPerFS {clockrate / (fs * 2)}; // probably 768 or 384. Definitely not more than 768.

	void dasm_all(const char *path, const char *mode = "w")
	{
		FILE *f = fopen(path, mode);
		for (int i = 0; i < 0xc00/4; i++) disassemble(i, f);
		fprintf(f, "\n\n");
		for (int i = 0x1000 / 4; i < 0x1c00/4; i++) disassemble(i, f);
		fprintf(f, "\n\n\n");
		fclose(f);
	}
	
	void transpile(FILE *f, bool eram = false)
	{
		struct opword { uint32_t coef, shift, mem, op, eram; };
		uint32_t *decode = (uint32_t*)(eram ? &intmem[0x1000] : &intmem[0]);
		opword pram[768];
		for (int i = 0; i < 768; i++)
		{
			uint32_t d = decode[i];
			pram[i] = {d & 255, (d >> 8) & 3, (d >> 10) & 255, (d >> 16) & 0x7c, (d >> 23) & 31};
		}

		// Decided limitations: We will not support op 0x34, mem & 0xcc == 0xc0 (these are the jump operations)
		for (int pc = 0; pc < 768; pc++) assert((pram[pc].op != 0xd || (pram[pc].mem & 0xcc) != 0xc0) && "Jumps!"); // jumps. bail.

		enum {kNone = 0, kSavesA = 1, kSavesB = 2};
		struct access {
			uint8_t save {kNone}; bool writesAccB {false}, clr {false}, nop {false}; // stage 1, extract these.
			bool accGetsUsed {false}, nomac {false}; uint16_t writePC {0}; // stage 2, compute this
			int readReg {-1}, srcReg {-1}, destReg {-1}; // stage 3, compute this
		};
		const access wA = {kNone, false}, wB = {kNone, true}, swA = {kSavesA, false}, swB = {kSavesB, true}, sBwA = {kSavesB, false}, sAwB = {kSavesA, true};

		access table[768];
		for (int pc = 0; pc < 768; pc++)
		{
			const opword o = pram[pc];
			access a = wA;
			switch (o.op)
			{
			case 0x00: case 0x04: case 0x28: case 0x2c: case 0x50: case 0x54: case 0x60: case 0x64: case 0x70: case 0x74: a = wA; break;
			case 0x08: case 0x38: case 0x40: case 0x44: case 0x48: case 0x4c: case 0x58: case 0x68: case 0x6c: case 0x78: case 0x7c: a = swA; break;
			case 0x0c: case 0x3c: a = sBwA; break;
			case 0x10: case 0x14: a = wB; break;
			case 0x18: a = sAwB; break;
			case 0x1c: case 0x5c: a = swB; break;
			case 0x20: case 0x24: a = (o.shift & 2) ? wB : wA; break;

			case 0x30: if (o.coef & 4) a = (o.coef & 2) ? swB : swA; else a = (o.coef & 2) ? wB : wA; break;
			case 0x34:
				if (o.mem >= 0xa0 && o.mem < 0xb0) a = (o.mem & 1) ? swB : swA;
					if (o.mem >= 0xc0) {
						switch (o.mem & 0xf)
						{
							case 0x7: case 0xa: case 0xb: case 0xc: case 0xd: case 0xe: case 0xf: a = (o.mem & 0x20) ? swB : swA; break;
							default: a = (o.mem & 0x20) ? wB : wA; break;
						}
					}
				break;
			default: break;
			}
			bool clr = false;
			switch (o.op)
			{
				case 0x04: case 0x08: case 0x0c: case 0x14: case 0x18: case 0x1c: case 0x24: case 0x44:
				case 0x4c: case 0x50: case 0x64: case 0x6c: case 0x74: case 0x7c: clr = true; break;
				case 0x30: clr = !(o.coef & 1); break;
				case 0x34: if (o.mem >= 0xc0) clr = (o.mem & 0x10); break;
				default: break;
			}
			a.clr = clr;
			if (!o.mem && !o.shift && !o.op && !o.coef) a.nop = true;
			if (o.op == 0x50) a.accGetsUsed = true;
			table[pc] = a;
		}
		
		for (int pc = 0; pc < 768; pc++)
		{
			if (table[pc].nop || !table[pc].save) continue;
			// this op saves a value. who generated it?
			bool savesB = (table[pc].save == kSavesB);
			int i = pc - 3;
			while (i >= 0) // find the instruction that generated this value.
			{
				if (table[i].nop || table[i].writesAccB != savesB) {i--; continue;} // this writes to the wrong accumulator. skip.
				table[i].accGetsUsed = true; // mark this write as being used.
				table[pc].writePC = i;
				break;
			}
		}

		for (int pc = 0; pc < 768; pc++) // does our mac achieve anything, in theory?
		{
			if (table[pc].accGetsUsed) continue; // yes.
			if (table[pc].nop) {table[pc].nomac = true; continue;} // no.
			int i = pc;
			while (++i < 768)
			{
				if (table[i].nop) continue; // dont care
				if (table[i].writesAccB != table[pc].writesAccB) continue; // doesn't apply to our acc. skip
				if (table[i].clr) {table[pc].nomac = true; break;} // we get overwritten
				if (table[i].accGetsUsed) break; // the result gets saved.
			}
		}
		
		int rega = 0, regb = 0;
		bool usedrega = false, usedregb = false;
		for (int pc = 0; pc < 768; pc++) // hand out register numbers
		{
			access &a = table[pc];
			if (table[pc].nop) continue;
			if (a.save) a.readReg = table[a.writePC].destReg;

			a.srcReg = (a.writesAccB) ? regb : rega;

			
			if (a.writesAccB && usedregb) {regb = (regb + 1) % 3; usedregb = false;}
			if (!a.writesAccB && usedrega) {rega = (rega + 1) % 3; usedrega = false;}

			a.destReg = (a.writesAccB) ? regb : rega;
			if (a.accGetsUsed) (a.writesAccB ? usedregb : usedrega) = true;
		}
		
		uint32_t eramword = 0;
		char temp[32], lastA[32];
		bool lastMul30 = false;

		// building blocks:
		// sat(%d) returns clamp(val, 0x800000, 0x7fffff)
		// gram(%d) returns gram[%d + irampos]
		// iram(%d) return iram[%d + irampos]
		// raw(%d) return val & 0xffffff
		// interp(%d) return (val >= 0) ? (~val & 0x7f) : (~(val & 0x7f))
		// interp24(%d) return (~val & 0x7fffff);
		// interp3(%d) return ((val >= 0) ? ~val : val) & 0x7fffff;
		// interp4(%d) return ((val < 0) ? ~val : val) & 0x7fffff;

		uint32_t eramPos = 0, eramImmOffsetAccNext = 0;
		uint16_t eramPCCommit = 0, eramPCStartNext = 0;
		uint8_t eramModeCurrent = 0, eramModeNext = 0;
		bool eramActiveCurrent = false, eramActiveNext = false;
		
		for (int pc = 0; pc < 768; pc++)
		{
			const opword o = pram[pc];
			if (!table[pc].nop || o.eram)
			{
				fprintf(f, "// %04x: ", pc + (eram ? 0x400 : 0));
				const uint8_t *from = (uint8_t*)&decode[pc];
				char buf[32] = "        "; if (o.eram) snprintf(buf, 32, "eram: %02x", o.eram);
				fprintf(f, "%02x %02x %02x %02x   %s opc:%02x mem:%02x shift:%01x coef:%02x\n", from[3], from[2], from[1], from[0], buf, o.op, o.mem, o.shift, o.coef);
			}

			if (eram)
			{
				constexpr int ERAM_COMMIT_STAGE = 10;
				uint8_t eramCtrl = o.eram;
				int stage1 = pc - eramPCStartNext;
				
				// Transaction start
				if (!eramActiveNext && ((eramCtrl & 0x18) != 0)) {
					eramActiveNext = true;
					eramModeNext = eramCtrl;
					eramPCStartNext = pc;
					eramImmOffsetAccNext = 0;
					stage1 = 0;
				}
				else if (eramActiveNext && stage1 <= 5 && stage1 > 0) eramImmOffsetAccNext += eramCtrl << ((stage1 - 1) * 5); // Accumulate immediates
				
				// Is it time to commit?
				if (eramActiveCurrent && (pc == eramPCCommit)) {
					if (eramModeCurrent == 0x10) fprintf(f, "eram[eramAddr] = eramWriteLatchNext;\n");	// write
					else fprintf(f, "eramReadLatch = se<24>(eram[eramAddr]);\n"); // read
					eramActiveCurrent = false; // done
				}
				
				// Next stage
				if (eramActiveNext && stage1 == 5) {
					eramActiveCurrent = true;
					eramModeCurrent = eramModeNext;
					eramPCCommit = eramPCStartNext + ERAM_COMMIT_STAGE;
					eramActiveNext = false;
					
					if (eramModeCurrent == 0x10) fprintf(f, "eramWriteLatchNext = eramWriteLatch;\n");
					// Addr computation
					if (eramModeNext == 0x18)
						fprintf(f, "eramAddr = (eramVarOffset >> 12) + ((0x%05x >> 1) & 1);\n", eramImmOffsetAccNext);
					else
						fprintf(f, "eramAddr = 0x%05x;\n", eramImmOffsetAccNext);

					// TODO: are there other modes? seems like no
				}
			}

			uint8_t shift = (0x3567 >> (o.shift << 2)) & 0xf; // this is shift amount. pick the value 3/5/6/7 using bits 8,9.
			char mulA[32], mulB[32];
			switch (o.mem)
			{
				case 1: strcpy(mulA, "0x10"); break; // 4
				case 2: strcpy(mulA, "0x400"); break; // 10
				case 3: strcpy(mulA, "0x10000"); break; // 16
				case 4: strcpy(mulA, "0x400000"); break; // 22
				default: snprintf(mulA, 32, "iram(0x%02x)", o.mem); break;
			}
			snprintf(mulB, 32, "0x%02x", o.coef);
			bool setcondition = false;
			char readAcc[32]; snprintf(readAcc, 32, "acc%c%d", (table[pc].save == kSavesB) ? 'B' : ((table[pc].save == kSavesA) ? 'A' : '?'), table[pc].readReg);

			switch (o.op)
			{
				case 0x08: case 0x0c: case 0x18: case 0x1c: case 0x58: case 0x5c: snprintf(mulA, 32, "sat(%s)", readAcc); fprintf(f, "setIram(0x%02x, %s);\n", o.mem, mulA); break;
				case 0x20: case 0x24: shift = (o.shift & 1) ? 6 : 7; snprintf(mulA, 32, "gram(0x%02x)", o.mem); break;

				case 0x30:
				{
					if (o.coef & 4) { snprintf(mulA, 32, "sat(%s)", readAcc); fprintf(f, "setIram(0x%02x, %s);\n", o.mem, mulA);	}
					char buf[32]; snprintf(buf, 32, "mulcoeff%d", o.coef >> 5);
					char sign[2] = "";
					if ((o.coef >> 5) == 6) snprintf(buf, 32, "eramVarOffset");
					if ((o.coef >> 5) == 7) snprintf(buf, 32, "mulcoeff5");
					if (o.coef & 8) strcpy(sign, "-");
					snprintf(mulB, 32, (o.coef & 16) ? "%sinterp(%s)" : "%s%s", sign, buf);
				}
					break;
				case 0x34:
					if (o.mem < 0xa0 || (o.mem & 0xf0) == 0xb0) printf("Unexpected value for mem (%02x) with opcode 0x34\n", o.mem);
					if (o.mem >= 0xa0 && o.mem < 0xb0) fprintf(f, "mulcoeff%d = sat(%s);\n", (o.mem >> 1) & 7, readAcc);
					if (o.mem >= 0xc0)
					{
						switch (o.mem & 0xf)
						{
							case 0x4: fprintf(f, "setIntPins();\n"); break;
							case 0x6:
								snprintf(mulA, 32, "(%s) >> 7", lastA);
								if (lastMul30) snprintf(mulB, 32, "(mulcoeff%d >> 9) & 0x7f", o.coef >> 5);
								break;
							case 0x7: fprintf(f, "eramVarOffset = full(%s);\n", readAcc); break;
							case 0xa: fprintf(f, "hostRegs = sat(%s);\n", readAcc); break;
							case 0xb: fprintf(f, "eramWriteLatch = sat(%s);\n", readAcc); break;
							case 0xc: case 0xd: case 0xe: case 0xf: snprintf(mulA, 32, "eramReadLatch"); fprintf(f, "setIram(0x%02x, %s);\n", o.mem | 0xf0, mulA); break;
							default: printf("Unknown value for mem (%02x) with opcode 0x34\n", o.mem); break;
						}
					}
					break;
				case 0x38: case 0x3c: snprintf(mulA, 32, "sat(%s)", readAcc); fprintf(f, "setGram(0x%02x, %s);\n", o.mem, mulA); break;
				
				case 0x40: case 0x44: snprintf(mulA, 32, "raw(%s)", readAcc); fprintf(f, "setIram(0x%02x, %s);\n", o.mem, mulA); break;
				case 0x48: case 0x4c: snprintf(mulA, 32, "rect(%s)", readAcc); fprintf(f, "setIram(0x%02x, %s);\n", o.mem, mulA); break;
				
				case 0x50: fprintf(f, "!!!COMPARE!!!\n"); setcondition = true; break;
				case 0x54: fprintf(f, "Mysterious opcode 54 at pc = %04x\n", pc - 1); break; // TODO: what is this?
				
				case 0x60: case 0x64: case 0x70: case 0x74: snprintf(temp, 32, "interp24(%s)", mulA); strcpy(mulA, temp); break;
				case 0x68: case 0x6c: fprintf(f, "setIram(0x%02x, sat(%s));\n", o.mem, readAcc); snprintf(mulA, 32, "interp3(sat(%s))", readAcc); break;
				case 0x78: case 0x7c: fprintf(f, "setIram(0x%02x, sat(%s));\n", o.mem, readAcc); snprintf(mulA, 32, "interp4(sat(%s))", readAcc); break;
				default: break;
			}

			const bool clr = table[pc].clr;
			if (!table[pc].nomac)
			{
				char srcAcc[32]; snprintf(srcAcc, 32, "acc%c%d", table[pc].writesAccB ? 'B' : 'A', table[pc].srcReg);
				char destAcc[32]; snprintf(destAcc, 32, "acc%c%d", table[pc].writesAccB ? 'B' : 'A', table[pc].destReg);

				if (clr) fprintf(f, "\t%s  = %s * %s >> %d;", destAcc, mulA, mulB, shift);
				else if (table[pc].srcReg != table[pc].destReg) fprintf(f, "\t%s  = %s + %s * %s >> %d;", destAcc, srcAcc, mulA, mulB, shift);
				else fprintf(f, "\t%s += %s * %s >> %d;", destAcc, mulA, mulB, shift);
				fprintf(f, "\n");
			}
			strcpy(lastA, mulA);
			lastMul30 = (o.op == 0x30);
		}
	}

	void disassemble(uint32_t address, FILE *f) {
		uint32_t opcode = 0;
		for (int i = 0; i < 4; i++) opcode |= intmem[address * 4 + i] << (i * 8);
		opcode &= 0xfffffff;
		int col = 0;
		col += fprintf(f, "%04x: ", address);
		if (address >= 1024)
		{
			const uint8_t dram = (opcode >> 23) & 0x1f;
			col += fprintf(f, "%01x%01x ", dram >> 1, dram & 1); // this is awkward but this is how we think about it right now, because this is byte aligned.
		}
		else if (opcode & 0x800000) col += fprintf(f, "<unexpected top bit set on low page> ");
		opcode &= 0x7fffff;
		col += fprintf(f, "%02x %02x %02x", (opcode >> 16) & 0x7f, (opcode >> 8) & 255, opcode & 255);
		if (!opcode) {fprintf(f, "\n"); return;}
		
		const uint8_t coeff = opcode & 0xff;
		opcode >>= 8;
		uint8_t shift = opcode & 3;
		opcode >>= 2;
		const uint8_t mem = opcode & 0xff;
		opcode >>= 8;
		const uint8_t opc = (opcode << 2) & 0x7c;

		char lastss[64]; strcpy(lastss, ss);

		const int shifts[4] = {7, 6, 5, 3};
		
		char ostr[64]; snprintf(ostr, sizeof(ostr), "[0x%02x]", mem);

		col += fprintf(f, "            ");
		switch (mem)
		{
			case 1: snprintf(ss, sizeof(ss), "0x10"); break;
			case 2: snprintf(ss, sizeof(ss), "0x400"); break;
			case 3: snprintf(ss, sizeof(ss), "0x1000"); break;
			case 4: snprintf(ss, sizeof(ss), "0x400000"); break;
			default: snprintf(ss, sizeof(ss), "iram%01d%s", (address >= 1024) ? 1 : 0, ostr); break;
		}

		char cstr[64]; snprintf(cstr, sizeof(cstr), "#0x%02x", coeff);
		char temp[64];
		strcpy(temp, ss);
		
		bool acc = false, nve = false;
		const char *macop = "MAC";
		switch (opc)
		{
			case 0x00: break;
			case 0x04: macop = "MUL"; break;
			case 0x08: macop = "MUL"; col += fprintf(f, "STSATA %s ", ss); strcpy(ss, "accA(t-3)"); break;
			case 0x0c: macop = "MUL"; col += fprintf(f, "STSATB %s ", ss); strcpy(ss, "accB(t-3)"); break;
			case 0x10: acc = true; break;
			case 0x14: macop = "MUL"; acc = true; break;
			case 0x18: macop = "MUL"; acc = true; col += fprintf(f, "STSATA %s ", ss); strcpy(ss, "accA(t-3)"); break;
			case 0x1c: macop = "MUL"; acc = true; col += fprintf(f, "STSATB %s ", ss); strcpy(ss, "accB(t-3)"); break;
			case 0x20: acc = shift & 2; shift &= 1; snprintf(ss, sizeof(ss), "gram%s", ostr); break;
			case 0x24: acc = shift & 2; shift &= 1; snprintf(ss, sizeof(ss), "gram%s", ostr); macop = "MUL"; break;
			case 0x28: macop = "<UNUSED>"; break;
			case 0x2c: macop = "<UNUSED>"; break;
			case 0x30:
			{
				acc = (coeff & 2);
				if (!(coeff & 1)) macop = "MUL";
				if (coeff & 4) strcpy(ss, "accA(t-3)");
				nve = (coeff & 8);
				if (coeff & 16) col += fprintf(f, "<Unknown bit 4 for op 0x30>");
				snprintf(cstr, sizeof(cstr), "mulcoeff%d", (coeff >> 5));
				break;
			}
			case 0x34: /// TODO:
			{
				if (mem < 0xa0 || (mem & 0xf0) == 0xb0) col += fprintf(f, "<UNEXPECTED mem %02x for op 0x34>", mem);
				if (mem >= 0xa0 && mem < 0xb0) col += fprintf(f,"mulcoeff%d = acc%c(t-3);", (mem >> 1) & 7, (mem & 1) ? 'B': 'A');
				if (mem >= 0xc0)
				{
					int sr = mem & 15;
					acc = (mem & 0x20);
					if (mem & 0x10) macop = "MUL";
					switch (sr)
					{
						case 0: col += fprintf(f, "JZ %03x", coeff); break;
						case 1: col += fprintf(f, "JMI %03x", coeff); break;
						case 2: col += fprintf(f, "JPL %03x", coeff); break;
						case 3: col += fprintf(f, "J   %03x", coeff); break;
						case 6: macop = "DMAC"; *ss = 0; break;
						case 7: col += fprintf(f, "ERAMVAR = acc%c;", acc ? 'B' : 'A'); break;
						case 10: col += fprintf(f, "HOSTREG = acc%c;", acc ? 'B' : 'A'); break;
						case 11: col += fprintf(f, "ERAMLAT = acc%c;", acc ? 'B' : 'A'); break;
						case 12: case 13: case 14: case 15:
							strcpy(ss, "ERAMRD");
							break;
						default: col += fprintf(f, "<UNKNOWN mem %02x for op 0x34>", mem); break;
					}
				}
				break;
			}
			case 0x38: col += fprintf(f, "gram%s = accA(t-3);", ostr); strcpy(ss,"accA(t-3)"); break;
			case 0x3c: col += fprintf(f, "gram%s = accB(t-3);", ostr); strcpy(ss,"accB(t-3)"); break;
			case 0x40: col += fprintf(f, "STA %s ", ss); break;
			case 0x44: macop = "MUL";  col += fprintf(f, "STA %s ", ss); break;
			case 0x48: col += fprintf(f, "RECTA %s ", ss); strcpy(ss, "accA(t-3)"); break;
			case 0x4c: macop = "MUL"; col += fprintf(f, "RECTA %s ", ss); strcpy(ss, "accA(t-3)");  break;
			case 0x50: macop = "CMP"; break;
			case 0x54: col += fprintf(f, "<MYSTERIOUS OP 54>"); break;
			case 0x58: col += fprintf(f, "STSATA %s ", ss); strcpy(ss, "accA(t-3)"); break;
			case 0x5c: acc = true; col += fprintf(f, "STSATB %s ", ss); strcpy(ss, "accB(t-3)"); break;
			case 0x70:
			case 0x60: snprintf(ss, sizeof(ss), "~%s & 0x7fffff", temp); break;
			case 0x74:
			case 0x64: macop = "MUL"; snprintf(ss, sizeof(ss), "~%s & 0x7fffff", temp); break;
			case 0x68: col += fprintf(f, "STSATA %s", ss); strcpy(ss, "mangle_pve(accA(t-3))"); break;
			case 0x6c: macop = "MUL"; col += fprintf(f, "STSATA %s", ss); strcpy(ss, "mangle_pve(accA(t-3))"); break;
			case 0x78: col += fprintf(f, "STSATA %s", ss); strcpy(ss, "abs(accA(t-3))"); break;
			case 0x7c: macop = "MUL"; col += fprintf(f, "STSATA %s", ss); strcpy(ss, "abs(accA(t-3))"); break;
			default: col += fprintf(f, "/* opc:%02x */ ", opc); break;
		}
		while (col < 60) col += fprintf(f, " ");

		col += fprintf(f, "              ");

		fprintf(f, "%-4s %c, %c%s >> %d, %s\n", macop, acc ? 'B' : 'A', nve ? '-' : ' ', cstr, shifts[shift], ss);
	}
	
	ESPCore<lg2eram_size> core0, core1;
	char ss[64];
	uint8_t if_mode = 0;
	uint32_t addr_sel = 0;
	uint8_t program_writing_word[4] {};
	uint8_t intmem[0x4000] = {0};
	SharedState<lg2eram_size> shared;
};
