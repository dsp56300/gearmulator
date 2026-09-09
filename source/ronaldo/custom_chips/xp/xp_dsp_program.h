#pragma once

// From PRAM/CRAM words to the lowered program. A word pair is decoded into a structural record that the
// lowering consumes and discards; nothing decoded is stored per slot at runtime. The numbers a program reads
// at runtime (coefficients, immediates, ERAM offsets) live in DspParams and are patched in place by host
// writes, so only a write that changes the structure of a slot invalidates the lowered program.
//
// The lowered form of a DSP program: a linear list of micro-ops with the chip's bookkeeping resolved at
// lowering time, for one frame configuration. A PRAM slot expands into 0..n ops; empty slots expand into
// none. This list is what the interpreter executes and what the JIT emits code from, op by op.
//
// Static programs (no reachable branch) get a static serial/DAC schedule (which IRAM word lands on which bus
// index), statically paired two-word ERAM commands with statically scheduled read landings, and a frame-end
// carry that the frame driver applies in one go. Only the leading slots whose pairing depends on the state
// left by the previous frame use the generic runtime ERAM logic.
//
// A program with forward branches whose skipped regions and frame-end window carry no serial event and no
// ERAM command keeps that static form with the branches in it (the jumps form): every schedule is the same on
// every path, only the slot that ends the frame depends on the skips taken. Anything else with a reachable
// branch, and every branching program of a lockstep pair, is lowered in dynamic mode: every slot keeps the
// runtime counters and the runtime ERAM pairing and read queue, and the frame driver follows the program
// counter.
//
// Mixer deposits are not ops: the frame driver applies the mixer sends of each cycle before the slot that
// runs in that cycle, exactly as the naive engine does, or hoists them ahead of the frame when the program
// never touches a deposited cell (mixerCells).

#include "xp_dsp_state.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace xpLib
{
	enum class DspTransfer : uint8_t
	{
		none,
		readState,        // loads iramReadLatch at the end of the slot
		readParameter,    // IRAM3 parameter cell: loads iram3ParameterLatch, leaves iramReadLatch alone
		writeEramReadLatch,
		writeAccumulator, // the pre-instruction accumulator
	};

	// This slot as the first word of a two-word ERAM command.
	enum class DspEramArm : uint8_t
	{
		none,
		read,
		writeIramLatch,
		writeAccumulator,
	};

	enum class DspSlotOp : uint8_t
	{
		none,
		primary,         // functions 1-F
		parallel,        // op 0x30: the CRAM word is the instruction
		indexedEramRead, // op 0x20
		branch,          // op 0x10
	};

	enum class DspMultiply : uint8_t
	{
		none,
		coefficient, // primary: sat29((input * param) / 8192)
		factor,      // parallel: sat29(((input * factor) << gainShift) / 32768)
	};

	enum class DspMultiplyInput : uint8_t
	{
		feedbackLatch,
		accumulatorSat24,
		iramReadLatch,
		eramReadLatch,
		serialNodeA,
		serialNodeB,
	};

	enum class DspMultiplyFactor : uint8_t
	{
		accLow12Shl3,
		accBits22to8,
		accShr8,
		iram3Parameter,
	};

	enum class DspAlu : uint8_t
	{
		hold,
		accPlusIram,
		accPlusMul,
		iram,
		mul,
		negAcc,
		iramMinusAcc,
		mulMinusAcc,
		iramPlusMul,
		minAccIram,
		maxAccIram,
		accPlusMulShr13,
		mulShr13,
		andImm,
		orImm,
		xorImm,
		minImm,
		maxImm,
		sameSignMinElseMax,
		sameSignMaxElseMin,
		accPlusImm,
		iramPlusImm,
		mulPlusImm,
		negAccPlusImm,
		accPlusMulPlusIram,
		iramPlusMulMinusAcc,
		accPlusMulMinusIram,
		mulMinusIramMinusAcc,
		mulMinusIram,
	};

	enum class DspTransform : uint8_t
	{
		none,
		absolute,
		sext24,
		lfsr,
		foldMirror,
		onesComplementNegative,
	};

	enum class DspBranch : uint8_t
	{
		never,
		always,
		eq0,
		ne0,
		ge0,
		lt0,
		gt0,
		le0,
	};

	// The IRAM bank an address selects. The two phase-relative banks are resolved per frame from iramSelPhase.
	enum class DspIramBank : uint8_t
	{
		mixer,      // memaddr 00-3f: the bank the mixer deposits into this frame
		processing, // memaddr 40-7f: the bank the mixer completed last frame
		direct1,    // memaddr 80-9f: IRAM1 cells 0x20-0x3f
		direct2,    // memaddr a0-bf: IRAM2 cells 0x20-0x3f
		iram3,      // memaddr c0-ff
	};

	// The number a slot reads from DspParams::param: the coefficient for functions 1-C, the 29-bit logical
	// immediate for D, the decoded immediate for E/F; zero otherwise.
	int32_t dspDecodeParam(uint32_t _pram, uint16_t _cram);

	// Host write classification. A CRAM write is data unless the CRAM word is the instruction (op 0x30) or
	// the branch (op 0x10). A PRAM write is data when it changes only ERAM offset bits and keeps the command
	// kind (eram bits 8:7).
	bool dspCramWriteIsStructural(uint32_t _pram);
	bool dspPramWriteIsStructural(uint32_t _oldPram, uint32_t _newPram);
	// Recomputes the ERAM offset tables for _slot and _slot + 1 from the raw program.
	void dspRefreshEramOffsets(const DspProgram& _program, DspParams& _params, size_t _slot);
	void dspRefreshParams(const DspProgram& _program, DspParams& _params);
} // namespace xpLib

namespace xpLib
{
	enum class FlatOpKind : uint8_t
	{
		// ERAM, static pairing. The write value register mirrors DspState::eramPendingWriteValue.
		eramArm,         // a = 0: capture the accumulator, 1: capture iramReadLatch into the write value
		eramWrite,       // index = second-word slot: eram[eramPos + eramOffset[slot]] = encode24(write value)
		eramLand,        // index = second-word slot, a = queue entry: eramReadLatch = eram[eramPos + eramOffset[slot]]
		eramIndexed,     // a = queue entry: eramIndexedOffset = (acc >> 12) & 0xffff, value[a] = eram[eramPos + it]
		eramLandIndexed, // a = queue entry: eramReadLatch = value[a]
		// ERAM, runtime pairing and read queue (the frame-start region and dynamic programs).
		eramGeneric,        // a = arm kind, index = slot: the naive first/second-word logic
		eramAdvance,        // land the runtime queue entries whose countdown expires
		eramIndexedGeneric, // queue a latency-2 read unless the data port is busy
		// Transfer. Reads are placed at the end of the slot so every other op sees the previous latch.
		iramWriteAcc,   // a = bank, b = cell
		iramWriteLatch, // a = bank, b = cell: IRAM <- eramReadLatch
		iramRead,       // a = bank, b = cell: iramReadLatch <- IRAM
		iramReadParam,  // b = IRAM3 cell: iram3ParameterLatch <- cell >> 10
		// Serial and DAC output, static schedule.
		emitA,   // a = bus A word index, b = processing-bank word
		emitBcd, // a = port 0-2, b = processing-bank word, c = bus word index
		emitADynamic,   // a = store words: the naive ioCtrl=1 logic on the runtime counters
		emitBcdDynamic, // a = per-port store mask: the naive ioCtrl=2 logic on the runtime counters
		pins,           // a = OUTP levels
		consumeA,
		consumeB,
		consumeBDynamic, // consume bus B when the runtime port position says this was the SDOB word
		// Datapath. Captures read the pre-slot latches; the multiply is committed after the ALU.
		mulCapture,     // a = multiplicand source, b = feedback source
		factorCapture,  // a = factor kind, b = complement
		alu,            // a = DspAlu, index = slot (param)
		parallel,       // a = DspAlu, b = transform, c = sext24 flag
		mulCoefficient, // index = slot: mul = sat29((input * param) / 8192), feedback = feedback source
		mulFactor,      // a = gain shift: mul = sat29(((input * factor) << a) / 32768)
		branch,         // a = condition, index = target slot (dynamic programs only)
	};

	struct FlatOp
	{
		FlatOpKind kind = FlatOpKind::eramAdvance;
		uint8_t a = 0;
		uint8_t b = 0;
		uint8_t c = 0;
		uint16_t index = 0;
	};

	// The per-frame inputs a lowering is specialised for. The boards set them once at initialisation.
	struct DspFrameConfig
	{
		uint8_t serialInputEnabled = 0;
		uint8_t serialOutputEnabled = 0; // effective: input and output enabled
		uint8_t consumeStagedBusA = 0;
		uint8_t bcdEnabled = 0; // bit p: port p (SDOB/SDOC/SDOD) is configured to output words
		uint8_t iram3ParameterReadBoundary = dsp::nIramSlots;
		uint8_t linked = 0; // one chip of a lockstep pair: branching programs take the dynamic form
		uint16_t executionSlots = dsp::nExecutionSlots;

		bool operator==(const DspFrameConfig& _other) const
		{
			return serialInputEnabled == _other.serialInputEnabled && serialOutputEnabled == _other.serialOutputEnabled &&
				consumeStagedBusA == _other.consumeStagedBusA && bcdEnabled == _other.bcdEnabled &&
				iram3ParameterReadBoundary == _other.iram3ParameterReadBoundary && linked == _other.linked &&
				executionSlots == _other.executionSlots;
		}
		bool operator!=(const DspFrameConfig& _other) const { return !(*this == _other); }
	};

	DspFrameConfig dspFrameConfig(const DspStepRequest& _request, bool _consumeStagedBusA, bool _linked = false);

	// A read still in flight when a static program reaches the end of its frame.
	struct FlatQueueEntry
	{
		int8_t countdown = -1;
		uint8_t indexed = 0;
		uint16_t slot = 0; // second-word slot of a normal read
	};

	// What the frame driver writes into DspState after the last slot of a static program.
	struct FlatCarry
	{
		uint32_t outputWordPosition = 0;
		uint8_t dacPortPosition = 0;
		std::array<uint8_t, dsp::nSerialBuses> serialOutputCount{};
		uint8_t staticRegion = 0;   // the static region is non-empty: the prefix flag and the queue are written
		uint8_t hasArm = 0;         // the static region armed a command: kind and high offset are written
		uint8_t lastArmIsWrite = 0;
		uint8_t prefixPendingAtEnd = 0;
		uint16_t lastArmSlot = 0;
		std::array<FlatQueueEntry, 2> queue{};
	};

	struct FlatProgram
	{
		std::vector<FlatOp> ops;
		// First op of each slot; [executionSlots..nProgramSlots] = end for static programs, [nProgramSlots] =
		// end for dynamic ones.
		std::array<uint16_t, dsp::nProgramSlots + 1> slotToOp{};
		DspFrameConfig config{};
		uint64_t generation = 0;
		bool dynamic = false;      // runtime counters and ERAM logic everywhere; the frame driver follows the pc
		bool jumps = false;        // static form with forward branches; the frame driver follows the pc
		uint16_t slotsLowered = 0; // slots [0, slotsLowered) have ops: the budget, the last reachable slot, or all
		uint16_t staticStart = 0;  // first slot with static ERAM pairing and read scheduling
		// Per IRAM phase: the mixer-bank cells (memaddr 00-3f, or the direct bank the mixer writes in that
		// phase) any lowered slot reads or writes. Deposits to other cells can be applied before the frame.
		std::array<uint64_t, 2> mixerCells{};
		FlatCarry carry{};
	};

	// Decodes and lowers the whole program for one configuration; refreshes every DspParams table.
	void flatLower(const DspProgram& _program, const DspFrameConfig& _config, FlatProgram& _flat, DspParams& _params);
} // namespace xpLib
