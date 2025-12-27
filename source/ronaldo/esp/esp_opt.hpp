#include <asmjit/asmjit.h>
#include <asmjit/a64.h>
#include <sstream>

#include "esp_jit_x64.h"
#include "esp_jit_arm64.h"
#include "esp_jit_types.h"

constexpr int PRAM_SIZE = 768;

struct CoreData {
  int32_t *hostRegPtr;
  int32_t *eramPtr;
  int64_t accs[6];
  int32_t mulcoeffs[8];
  int8_t coefs[PRAM_SIZE];
  int8_t shiftAmounts[PRAM_SIZE];
};

enum { kNone = 0, kSavesA = 1, kSavesB = 2 };

struct MemAccess
{
	uint8_t save{kNone};
	bool writesAccB{false}, clr{false}, nop{false}; // stage 1, extract these.
	bool accGetsUsed{false}, nomac{false};
	uint16_t writePC{0}; // stage 2, compute this
	int readReg{-1}, srcReg{-1}, destReg{-1}; // stage 3, compute this
};

enum ESPInstrOptType
{
	kNop, kMAC, kStoreIRAM, kStoreGRAM, kMulCoef, kWriteMulCoef, kDMAC,
	kWriteEramVarOffset, kWriteEramWriteLatch, kReadEramReadLatch, kWriteHost,
	kReadGRAM, kStoreIRAMUnsat, kStoreIRAMRect, kSetCondition, kInterp,
	kInterpStorePos, kInterpStoreNeg
};

struct ESPOptInstr
{
	// raw data
	uint32_t coef = 0, shift = 0, mem = 0, op = 0;
	int8_t coefSigned = 0;

	// processed data
	uint8_t shiftAmount = 0;
	bool useImm = false;
	int32_t imm = 0;
	ESPInstrOptType opType = kMAC;

	// from analysis
	MemAccess m_access;
	bool skippablePos = false, skippableNeg = false;

	ESPOptInstr()
	= default;

	ESPOptInstr(uint32_t instr)
	{
		mem = (instr >> 10) & 0xff;
		op = (instr >> 16) & 0x7c;
		coef = instr & 0xff;
		coefSigned = se<8>(coef);
		shift = (instr >> 8) & 3;
		shiftAmount = (0x3567 >> (shift << 2)) & 0xf; // this is shift amount. pick the value 3/5/6/7 using bits 8,9.
		if (op == 0x20 || op == 0x24) shiftAmount = (shift & 1) ? 6 : 7;

		switch (mem)
		{
		case 1: useImm = true;
			imm = 0x10;
			break; // 4
		case 2: useImm = true;
			imm = 0x400;
			break; // 10
		case 3: useImm = true;
			imm = 0x10000;
			break; // 16
		case 4: useImm = true;
			imm = 0x400000;
			break; // 22
		}

		if (op == 0 && mem == 0 && shift == 0 && coef == 0)
		{
			opType = kNop;
		}
		else
		{
			switch (op)
			{
			case 0x08:
			case 0x0c:
			case 0x18:
			case 0x1c:
			case 0x58:
			case 0x5c: opType = kStoreIRAM;
				break;
			case 0x20:
			case 0x24: opType = kReadGRAM;
				break;
			case 0x30: opType = kMulCoef;
				break;

			case 0x34:
				if (mem < 0xa0 || (mem & 0xf0) == 0xb0) printf("Unexpected value for mem (%02x) with opcode 0x34\n",
				                                               mem);
				if (mem >= 0xa0 && mem < 0xb0) opType = kWriteMulCoef;
				if (mem >= 0xc0)
				{
					switch (mem & 0xf)
					{
					// unsupported stuff
					case 0x0:
					case 0x1:
					case 0x2:
					case 0x3:
						printf("jump!\n");
						opType = kNop;
						break;
					case 0x4:
						printf("int pins!\n");
						break;

					case 0x6: opType = kDMAC;
						break;
					case 0x7: opType = kWriteEramVarOffset;
						break;
					case 0xa: opType = kWriteHost;
						break;
					case 0xb: opType = kWriteEramWriteLatch;
						break;
					case 0xc:
					case 0xd:
					case 0xe:
					case 0xf:
						opType = kReadEramReadLatch;
						break;
					default:
						printf("Unknown value for mem (%02x) with opcode 0x34\n", mem);
						break;
					}
				}
				break;

			case 0x38:
			case 0x3c: opType = kStoreGRAM;
				break;
			case 0x40:
			case 0x44: opType = kStoreIRAMUnsat;
				break;
			case 0x48:
			case 0x4c: opType = kStoreIRAMRect;
				break;
			case 0x50: opType = kSetCondition;
				printf("CONDITIONS!\n");
				break;
			case 0x60:
			case 0x64:
			case 0x70:
			case 0x74: opType = kInterp;
				break;
			case 0x68:
			case 0x6c: opType = kInterpStorePos;
				break;
			case 0x78:
			case 0x7c: opType = kInterpStoreNeg;
				break;

			case 0x54:
			case 0x28:
			case 0x2c: printf("Mysterious opcode %02x\n", op);
				break; // TODO: what is this?
			}
		}
	}
};

template<int lg2eram_size>
class ESPOptimizer
{
public:
  ESPOptimizer(ESP<lg2eram_size>* esp) : m_esp(esp), logger(fopen("esp_jit.log", "w"))
  {
    data_core0.hostRegPtr = (int32_t*)esp->shared.readback_regs;
    data_core0.eramPtr = &esp->shared.eram.eram[0];
    data_core1.hostRegPtr = (int32_t*)esp->shared.readback_regs;
    data_core1.eramPtr = &esp->shared.eram.eram[0];
  }

  void genProgram(ESP<lg2eram_size>* esp)
  {
    if (runCore0) m_rt.release(runCore0);
    if (runCore1) m_rt.release(runCore1);

    eramEmitter.init(esp);
    coreEmitter0.init(esp, &esp->core0);
    coreEmitter1.init(esp, &esp->core1);

    updateCoef(esp);

    // logger.log("#### CORE 0 ####\n");
    genCore(esp, 0, &coreEmitter0, &runCore0);
    
    // logger.log("\n\n\n#### CORE 1 ####\n");
    genCore(esp, 1, &coreEmitter1, &runCore1, true);

    // fflush(logger._file);
    // printf("JITed ESP cores\n");
  }
  
  void setProgramDirty()
  {
	  m_programDirty = 3;
  }

  void genProgramIfDirty()
  {
      if (m_programDirty > 0)
      {
          if (--m_programDirty == 0)
              genProgram(m_esp);
	  }
  }

  void updateCoef(ESP<lg2eram_size>* esp)
  {
    for (size_t i = 0; i < PRAM_SIZE; i++) {
      uint32_t instr = esp->core0.pram[i];
      uint32_t op = (instr >> 16) & 0x7c;
      int8_t coef = se<8>(instr & 0xff);
      uint32_t shift = (instr >> 8) & 3;
      uint32_t shiftAmount = (0x3567 >> (shift << 2)) & 0xf;
      if (op == 0x20 || op == 0x24) shiftAmount = (shift & 1) ? 6 : 7;
      data_core0.coefs[i] = coef;
      data_core0.shiftAmounts[i] = shiftAmount;
    }

    for (size_t i = 0; i < PRAM_SIZE; i++) {
      uint32_t instr = esp->core1.pram[i];
      uint32_t op = (instr >> 16) & 0x7c;
      int8_t coef = se<8>(instr & 0xff);
      uint32_t shift = (instr >> 8) & 3;
      uint32_t shiftAmount = (0x3567 >> (shift << 2)) & 0xf;
      if (op == 0x20 || op == 0x24) shiftAmount = (shift & 1) ? 6 : 7;
      data_core1.coefs[i] = coef;
      data_core1.shiftAmounts[i] = shiftAmount;
    }
  }

  inline void callOptimized(ESP<lg2eram_size>* esp)
  {
    if (runCore0) runCore0(data_core0.coefs, esp->core0.iram, esp->shared.gram, &data_core0, esp->shared.eram.eramPos, esp->core0.iramPos, 0, 0);
    if (runCore1) runCore1(data_core1.coefs, esp->core1.iram, esp->shared.gram, &data_core1, esp->shared.eram.eramPos, esp->core1.iramPos, 0, 0);
  }
  
private:
  class CoreEmitter;

  ESP<lg2eram_size>* m_esp;
  asmjit::JitRuntime m_rt;
  asmjit::FileLogger logger;
  uint32_t m_programDirty = 0;
  
  typedef void(*RunCore)(int8_t* coefsPtr, int32_t *iramPtr, int32_t *gramPtr, CoreData *varPtr, uint32_t eramPos, uint32_t iramPos, int64_t unused1, int64_t unused2);
  RunCore runCore0 = nullptr, runCore1 = nullptr;

  // State used by jitted code
  CoreData data_core0{0};
  CoreData data_core1{0};

  void genCore(ESP<lg2eram_size>* esp, uint32_t core, CoreEmitter* emitter, RunCore *dest, bool withEram = false)
  {
    // TODO: do we need a new CodeHolder each time?
    asmjit::CodeHolder code;
    code.init(m_rt.environment());

  	logger.addFlags(asmjit::FormatFlags::kHexImms | /*asmjit::FormatFlags::kHexOffsets |*/ asmjit::FormatFlags::kMachineCode);

//	code.setLogger(&logger);
    
    esp::Builder m_asm(&code);

	CoreData& coreData = core ? data_core1 : data_core0;
	ESPCore<lg2eram_size>& espCore = core ? esp->core1 : esp->core0;

    esp::JitInputData jitData;
	jitData.coreData = &coreData;
	jitData.iram = espCore.iram;
	jitData.gram = esp->shared.gram;

  	jitData.eramPos = &esp->shared.eram.eramPos;
	jitData.iramPos = &espCore.iramPos;

	jitData.eramEffectiveAddr = &esp->shared.eram.eramEffectiveAddr;
	jitData.eramWriteLatchNext = &esp->shared.eram.eramWriteLatchNext;

  	jitData.eramReadLatch = &esp->shared.eram.eramReadLatch;
	jitData.eramWriteLatch = &esp->shared.eram.eramWriteLatch;
	jitData.eramVarOffset = &esp->shared.eram.eramVarOffset;
	jitData.last_mulInputA_24 = &espCore.last_mulInputA_24;
	jitData.last_mulInputB_24 = &espCore.last_mulInputB_24;

	esp::EspJit jit(m_asm, jitData);

    // m_asm.addDiagnosticOptions(asmjit::DiagnosticOptions::kValidateAssembler);
    // m_asm.addDiagnosticOptions(asmjit::DiagnosticOptions::kValidateIntermediate);

	jit.jitEnter();

    for (size_t pc = 0; pc < PRAM_SIZE; pc++)
    {
      if (withEram)
        eramEmitter.emit(pc, jit, m_asm);
      emitter->emit(pc, jit, m_asm);
    }

    // logger.log("---- ending ----\n");
    emitter->emitEnd(m_asm);

	jit.jitExit();

    m_asm.finalize();
    
    const auto err = m_rt.add(dest, &code);
    if (err)
    {
      const auto* const errString = asmjit::DebugUtils::errorAsString(err);
      std::stringstream ss;
      ss << "JIT failed: " << err << " - " << errString;
      const std::string msg(ss.str());
      throw std::runtime_error(msg);
    }
  }

  class ERAMEmitter
  {
  public:
    void init(ESP<lg2eram_size>* _esp)
    {
      esp = _esp;

      eramPCCommit = 0, eramPCStartNext = 0;
      eramModeCurrent = 0, eramModeNext = 0;
      eramImmOffsetAccNext = 0;
      eramActiveCurrent = false, eramActiveNext = false;
      highOffset = false;
    }

    void emit(int pc, esp::EspJit& _jit, esp::Builder& m_asm)
    {
      if (lg2eram_size == 0) return;

      uint32_t *decode = (uint32_t*)(&esp->intmem[0x1000]);
      uint32_t eramCtrl = (decode[pc] >> 23) & 0x1f;
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
        if (eramModeCurrent == 0x10) emitWrite(_jit, m_asm);
        else emitRead(_jit, m_asm);
        eramActiveCurrent = false; // done
      }

      // Next stage
      if (eramActiveNext && stage1 == 5) { // FIXME: stage1 should be 4, but there are some problems with latching
        if (eramActiveCurrent) printf("ERAM transaction already active at pc %03x\n", pc);
        eramActiveCurrent = true;
        eramModeCurrent = eramModeNext;
        eramPCCommit = eramPCStartNext + ERAM_COMMIT_STAGE;
        eramActiveNext = false;

        // Addr computation
        uint32_t immOffset = eramImmOffsetAccNext;
        bool shouldUseVarOffset = false;
        if (eramModeNext == 0x18)
        {
          immOffset = (eramImmOffsetAccNext >> 1) & 1;
          highOffset = eramImmOffsetAccNext & 0x100;
          shouldUseVarOffset = true;
        }

        emitComputeAddr(_jit, m_asm, immOffset, highOffset, shouldUseVarOffset);
      }
    }

  private:
    // WARNING: shares regs as a core

    void emitWrite(esp::EspJit& _jit, esp::Builder& m_asm)
    {
		_jit.eramWrite(ERAM_MASK);
    }

    void emitRead(esp::EspJit& _jit, esp::Builder& m_asm)
    {
		_jit.eramRead(ERAM_MASK);
    }

    void emitComputeAddr(esp::EspJit& _jit, esp::Builder& m_asm, uint32_t immOffset, bool highOffset, bool shouldUseVarOffset)
    {
		_jit.eramComputeAddr(immOffset, highOffset, shouldUseVarOffset);
    }
  
    uint16_t eramPCCommit = 0, eramPCStartNext = 0;
    uint8_t eramModeCurrent = 0, eramModeNext = 0;
    uint32_t eramImmOffsetAccNext = 0;
    bool eramActiveCurrent = false, eramActiveNext = false;
    bool highOffset = false;

    ESP<lg2eram_size>* esp;
    static constexpr int64_t ERAM_COMMIT_STAGE = 10, ERAM_MASK_FULL = (1 << 19) - 1;
		enum {eram_size = 1 << lg2eram_size, ERAM_MASK = eram_size - 1};
  };
  ERAMEmitter eramEmitter;

  class CoreEmitter
  {
  public:
    void init(ESP<lg2eram_size>* _esp, ESPCore<lg2eram_size>* _core)
    {
      esp = _esp;
      core = _core;

      pre_optimize();
    }

    void emit(int pc, esp::EspJit& _jit, esp::Builder& m_asm)
    {
		const ESPOptInstr &instr = pram_opt[pc];

    	if (instr.opType == kNop) return;

		_jit.emitOp(pc, instr, lastMul30);

		lastMul30 = (instr.op == 0x30);
    }

    void emitEnd(esp::Builder& m_asm)
    {
      // Store back accumulators
      // m_asm.mov(x8, uint64_t(&acc[0]));
      // m_asm.str(x0, ptr(x8));
      // m_asm.str(x1, ptr(x8, 1 << 3));
      // m_asm.str(x2, ptr(x8, 2 << 3));
      // m_asm.str(x3, ptr(x8, 3 << 3));
      // m_asm.str(x4, ptr(x8, 4 << 3));
      // m_asm.str(x5, ptr(x8, 5 << 3));

			// not done for now, should not be necessary
    }
  
    bool lastMul30 = false;

		void pre_optimize()
		{
			for (int i = 0; i < PRAM_SIZE; i++)
				pram_opt[i] = ESPOptInstr(core->pram[i]);

			// Decided limitations: We will not support op 0x34, mem & 0xcc == 0xc0 (these are the jump operations)
			for (int pc = 0; pc < PRAM_SIZE; pc++) assert((pram_opt[pc].op != 0xd || (pram_opt[pc].mem & 0xcc) != 0xc0) && "Jumps!"); // jumps. bail.

			const MemAccess wA = {kNone, false}, wB = {kNone, true}, swA = {kSavesA, false}, swB = {kSavesB, true}, sBwA = {kSavesB, false}, sAwB = {kSavesA, true};

			// Decode DSP instructions and figure out memory accesses
			for (int pc = 0; pc < PRAM_SIZE; pc++)
			{
				ESPOptInstr &o = pram_opt[pc];
				MemAccess a = wA;
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
					if (o.mem >= 0xa0 && o.mem < 0xb0) a = (o.mem & 1) ? sBwA : swA;
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
				o.m_access = a;
			}
			
			for (int pc = 0; pc < PRAM_SIZE; pc++)
			{
				if (pram_opt[pc].m_access.nop || !pram_opt[pc].m_access.save) continue;
				// this op saves a value. who generated it?
				bool savesB = (pram_opt[pc].m_access.save == kSavesB);
				int i = pc - 3;
				while (i >= 0) // find the instruction that generated this value.
				{
					if (pram_opt[i].m_access.nop || pram_opt[i].m_access.writesAccB != savesB) {i--; continue;} // this writes to the wrong accumulator. skip.
					pram_opt[i].m_access.accGetsUsed = true; // mark this write as being used.
					pram_opt[pc].m_access.writePC = i;
					break;
				}
			}

			for (int pc = 0; pc < PRAM_SIZE; pc++) // does our mac achieve anything, in theory?
			{
				if (pram_opt[pc].m_access.accGetsUsed) continue; // yes.
				if (pram_opt[pc].m_access.nop) {pram_opt[pc].m_access.nomac = true; continue;} // no.
				int i = pc;
				while (++i < PRAM_SIZE)
				{
					if (pram_opt[i].m_access.nop) continue; // dont care
					if (pram_opt[i].m_access.writesAccB != pram_opt[pc].m_access.writesAccB) continue; // doesn't apply to our acc. skip
					if (pram_opt[i].m_access.clr) {pram_opt[pc].m_access.nomac = true; break;} // we get overwritten
					if (pram_opt[i].m_access.accGetsUsed) break; // the result gets saved.
				}
			}
			
			int rega = 0, regb = 0;
			bool usedrega = false, usedregb = false;
			for (int pc = 0; pc < PRAM_SIZE; pc++) // hand out register numbers
			{
				MemAccess &a = pram_opt[pc].m_access;
				if (pram_opt[pc].m_access.nop) continue;
				if (a.save) a.readReg = pram_opt[a.writePC].m_access.destReg;

				a.srcReg = (a.writesAccB) ? regb : rega;
				
				if (a.writesAccB && usedregb) {regb = (regb + 1) % 3; usedregb = false;}
				if (!a.writesAccB && usedrega) {rega = (rega + 1) % 3; usedrega = false;}

				a.destReg = (a.writesAccB) ? regb : rega;
				if (a.accGetsUsed) (a.writesAccB ? usedregb : usedrega) = true;
			}

			for (int pc = 0; pc < PRAM_SIZE; pc++) // flatten accA and accB into single array
			{
				MemAccess &a = pram_opt[pc].m_access;
				if (a.writesAccB) { a.srcReg += 3; a.destReg += 3; }
				if (a.save == kSavesB) a.readReg += 3;
			}

			int32_t skipfieldPos = 0;
			int32_t skipfieldNeg = 0;
			for (int pc = 0; pc < PRAM_SIZE; pc++) // decode op50 skip (unused for now)
			{
				pram_opt[pc].skippablePos = skipfieldPos & 1;
				pram_opt[pc].skippableNeg = skipfieldNeg & 1;

				skipfieldPos >>= 1;
				skipfieldNeg >>= 1;
				
				if (pram_opt[pc].op == 0x50)
				{
					skipfieldPos |= 0x30;
					skipfieldNeg |= 0x3c0;
				}
			}
		}

    ESP<lg2eram_size>* esp;
    ESPCore<lg2eram_size>* core;

    ESPOptInstr pram_opt[PRAM_SIZE] {};
  };
  CoreEmitter coreEmitter0, coreEmitter1;
};
