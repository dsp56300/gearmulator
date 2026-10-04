#pragma once

// The per-frame runtime image of one ESP: everything the running program mutates.
// The interpreter and the native kernels share one image, so either engine resumes
// from the state the other left.

#include "esp_common.h"

namespace esp
{
	// Signed 30-bit wrapping accumulator with the three-clock delay line every
	// consumer reads from. outputClear models 2005 bits 5/6 (zero exports).
	struct Accumulator
	{
		static constexpr int delay = 3;
		int32_t acc = 0;
		int32_t hist[delay] = {};
		int32_t head = 0;
		bool outputClear = false;

		void reset() { acc = head = 0; hist[0] = hist[1] = hist[2] = 0; outputClear = false; }
		void set(int32_t v) { acc = se<30>(v); }
		void add(int32_t v) { acc = se<30>(acc + v); }
		int32_t rawFull() const { return acc; }
		int32_t pipelineRawFull() const { return hist[head]; }
		int32_t pipelineOutputFull() const { return outputClear ? 0 : hist[head]; }
		int32_t pipelineSat24() const
		{
			const int32_t v = pipelineOutputFull();
			return v > 0x7fffff ? 0x7fffff : v < -0x800000 ? -0x800000 : v;
		}
		int32_t pipelineRaw24() const { return se<24>(pipelineOutputFull()); }
		void storePipeline() { hist[head] = acc; if (++head == delay) head = 0; }
		// k >= 3 pipeline steps with an unchanged accumulator: the history is
		// entirely the current value and the head position is what it would be.
		void fastForward(unsigned k) { hist[0] = hist[1] = hist[2] = acc; head = int32_t((head + k) % delay); }
	};

	// IRAM and GRAM are mirrored rings: logical slot a lives at index a when
	// a >= iramPos, else at a + IRAM_SIZE, so index iramPos + mem never wraps for
	// mem in 0..255 and a compiled frame addresses every slot with an immediate
	// offset from &ring[iramPos]. A position change relocates the slots whose
	// home moves: one word per decrement, a block copy on the wrap, a full
	// re-layout for the freeze position.
	static constexpr unsigned RING_SIZE = 2 * IRAM_SIZE;
	static constexpr uint32_t ringSlot(uint32_t logical, uint32_t position) { return logical >= position ? logical : logical + IRAM_SIZE; }
	inline void relocateRing(int32_t* ring, uint32_t oldPos, uint32_t newPos)
	{
		if (newPos == oldPos) return;
		if (newPos == oldPos - 1) { ring[newPos] = ring[newPos + IRAM_SIZE]; return; }
		if (oldPos == 0 && newPos == IRAM_MASK) { std::memcpy(&ring[IRAM_SIZE], &ring[0], IRAM_MASK * sizeof(int32_t)); return; }
		int32_t logical[IRAM_SIZE];
		for (uint32_t a = 0; a < IRAM_SIZE; ++a) logical[a] = ring[ringSlot(a, oldPos)];
		for (uint32_t a = 0; a < IRAM_SIZE; ++a) ring[ringSlot(a, newPos)] = logical[a];
	}

	struct CoreRuntime
	{
		int32_t iram[RING_SIZE] = {};
		uint32_t iramPos = 0;
		Accumulator accA, accB;
		uint32_t pc = 0;
		int32_t pcjumpat = -1, pcjumpto = -1;
		int32_t lastMulInputA = 0;	// retained multiplicand (DMAC, tail)
		uint8_t lastCoefficientTransform = 0;
		bool lastMul30 = false;
		// CMP predicate pipeline (see esp_interpreter.h finishCondition).
		uint16_t skipfieldPos = 0, skipfieldNeg = 0;
		uint8_t predicateReady = 0, predicateValues = 0;
		bool comparePending = false, compareNegative = false;
		// IRQ latches written by opcode 34 sub-operations 4 and 5.
		uint8_t dintPin = 0;
		bool irq1Sign = false;

		void reset()
		{
			std::memset(iram, 0, sizeof(iram));
			iramPos = pc = 0;
			pcjumpat = pcjumpto = -1;
			lastMulInputA = 0; lastCoefficientTransform = 0; lastMul30 = false;
			skipfieldPos = skipfieldNeg = 0; predicateReady = predicateValues = 0;
			comparePending = compareNegative = false;
			dintPin = 0; irq1Sign = false;
			accA.reset(); accB.reset();
		}
		void sync(bool advanceMemory)
		{
			pc = 0;
			if (!advanceMemory) return;
			const uint32_t before = iramPos;
			iramPos = (iramPos - 1) & IRAM_MASK;
			relocateRing(iram, before, iramPos);
		}
		void freeze()
		{
			const uint32_t before = iramPos;
			iramPos = freezePosition(iramPos, IRAM_MASK);
			relocateRing(iram, before, iramPos);
		}
		uint32_t ringIndex(uint32_t offset) const { return iramPos + (offset & IRAM_MASK); }
		int32_t readIram(uint32_t offset) const { return iram[ringIndex(offset)]; }
		void writeIram(int32_t value, uint32_t offset) { iram[ringIndex(offset)] = value; }
		// Predicate state that a NOP run cannot fast-forward through.
		bool predicateIdle() const { return !comparePending && predicateReady == 0; }
	};

	// ERAM controller: command collection, address formation and the transfer
	// schedule of the delay memory.
	template <int lg2eramSize>
	struct EramRuntime
	{
		static constexpr unsigned size = lg2eramSize ? 1u << lg2eramSize : 1;
		static constexpr uint32_t mask = size - 1;
		std::unique_ptr<int32_t[]> memory;	// heap: up to 2 MiB
		// Compressed-mode shadow: packed[i] == packEram(memory[i]) whenever
		// compress is set (rebuilt on entering the mode), so reads and the phase
		// merges of the byte protocol work on the 16-bit code directly.
		std::unique_ptr<uint16_t[]> packed;

		int32_t readLatch = 0, writeLatch = 0, varOffset = 0;
		uint16_t readPacked = 0, readPackedNext = 0, writePackedNext = 0;
		uint32_t pos = 0, effectiveAddr = 0, immOffsetAccNext = 0, highOffset = 0, varOffsetNext = 0;
		int32_t writeLatchNext = 0;
		uint64_t cycle = 0, cycleCommit = 0, cycleStartNext = 0, cycleStartCurrent = 0, nextCommandCycle = 0;
		uint8_t modeCurrent = 0, modeNext = 0;
		bool activeCurrent = false, activeNext = false, shortCurrent = false, shortNext = false;
		uint32_t lowEffectiveAddr = 0;
		int32_t readRawNext = 0;
		uint8_t deferredCommand = 0;
		// Configuration-derived (2003 bits 6:5, 2005 bit4).
		uint8_t memoryMode = 0x40;
		bool compress = true, staticBus = false;
		// Derived from the three above, which only change between frames.
		bool m_rawSplit = false, m_rawSingle = false, m_extended = false;
		unsigned m_spacing = 5;

		EramRuntime() : memory(new int32_t[size]), packed(new uint16_t[size])
		{
			std::memset(memory.get(), 0, size * sizeof(int32_t));
			std::memset(packed.get(), 0, size * sizeof(uint16_t));
		}

		void reset()
		{
			std::memset(memory.get(), 0, size * sizeof(int32_t));
			std::memset(packed.get(), 0, size * sizeof(uint16_t));
			readLatch = writeLatch = varOffset = 0;
			readPacked = readPackedNext = writePackedNext = 0;
			pos = effectiveAddr = immOffsetAccNext = highOffset = varOffsetNext = 0;
			writeLatchNext = 0;
			cycle = cycleCommit = cycleStartNext = cycleStartCurrent = nextCommandCycle = 0;
			modeCurrent = modeNext = 0;
			activeCurrent = activeNext = shortCurrent = shortNext = false;
			lowEffectiveAddr = 0; readRawNext = 0; deferredCommand = 0;
		}
		void configureMemory(uint8_t config2003) { memoryMode = config2003 & 0x60; setCompression((config2003 & 0x40) != 0); }
		void setCompression(bool enable)
		{
			if (enable && !compress) rebuildPacked();	// raw-mode writes bypass the shadow
			compress = enable;
			updateMode();
		}
		void rebuildPacked() { for (unsigned i = 0; i < size; ++i) packed[i] = packEram(memory[i]); }
		void configureBus(uint8_t config2005) { staticBus = (config2005 & 0x10) != 0; updateMode(); }
		void updateMode()
		{
			m_rawSplit = !compress && memoryMode == 0x20;
			m_rawSingle = !compress && memoryMode == 0;
			m_extended = staticBus || memoryMode == 0x60;
			m_spacing = memoryMode == 0x60 || (staticBus && m_rawSplit) ? 8 : m_rawSingle && !staticBus ? 4 : 5;
		}
		bool active() const { return activeCurrent || activeNext; }
		bool rawSplit() const { return m_rawSplit; }
		bool rawSingle() const { return m_rawSingle; }
		bool extendedSchedule() const { return m_extended; }
		unsigned commandSpacing() const { return m_spacing; }
		// One controller clock; an idle controller only counts.
		ESP_ALWAYS_INLINE void clock(uint8_t field, bool acceptCommand, bool transferEnabled)
		{
			if (!field && !active() && !deferredCommand) ++cycle;
			else tick(field, acceptCommand, transferEnabled);
		}
		void tickSample() { pos = (pos - 1) & ERAM_MASK_FULL; }
		void freeze() { pos = freezePosition(pos, ERAM_MASK_FULL); }

		int32_t readRaw(uint32_t address) const { return memory[address & mask]; }
		uint16_t readPackedAt(uint32_t address) const { return compress ? packed[address & mask] : packEram(memory[address & mask]); }
		void setReadPacked(uint16_t value) { readPacked = value; readLatch = unpackEram(value); }
		void writePhase(uint32_t address, unsigned phase, int32_t value)
		{
			value = se<24>(value);
			auto& word = memory[address & mask];
			if (rawSingle()) word = value;
			else if (compress)
			{
				const uint16_t m = phase ? 0xff00 : 0x00ff;
				auto& code = packed[address & mask];
				code = uint16_t((code & ~m) | (packEram(value) & m));
				word = unpackEram(code);
			}
			else
			{
				const uint32_t m = phase ? 0xfff000 : 0x000fff;
				word = se<24>(int32_t((uint32_t(word) & ~m) | (uint32_t(value) & m)));
			}
		}

		void tickExtendedTransfer(uint64_t _cycle, bool transferEnabled)
		{
			const unsigned stage = unsigned(_cycle - cycleStartCurrent);
			const bool write = (modeCurrent & 0x18) == 0x10;
			unsigned shift = 0, bits = 0, finish = 10;
			if (staticBus)
			{
				if (rawSingle()) { if (stage == 6) bits = 24; finish = 6; }
				else if (stage == 6 || stage == 10) { bits = compress ? 8 : 12; shift = stage == 10 ? bits : 0; }
			}
			else
			{
				const unsigned phase = stage - (write ? 1 : 0);
				if (phase == 5 || phase == 6 || phase == 8 || phase == 9)
				{
					bits = 4;
					shift = (phase == 5 ? 0 : phase == 6 ? 1 : phase == 8 ? 2 : 3) * 4;
				}
				finish = write ? 10 : 9;
			}
			if (bits && transferEnabled)
			{
				const uint32_t m = ((1u << bits) - 1) << shift;
				const uint32_t value = write ? (compress ? packEram(writeLatch) : uint32_t(writeLatch))
					: staticBus ? (compress ? readPacked : uint32_t(readLatch)) : readPackedAt(effectiveAddr);
				if (!(staticBus && write && (rawSingle() || memoryMode == 0x60)))
				{
					if (compress) setReadPacked(uint16_t((readPacked & ~m) | (value & m)));
					else readLatch = se<24>(int32_t((uint32_t(readLatch) & ~m) | (value & m)));
				}
			}
			if (stage >= finish) activeCurrent = false;
		}

		// One DSP clock of the controller. field = core1's control field this
		// clock (0 when core1 is not running or past normal decode).
		ESP_ALWAYS_INLINE void tick(uint8_t field, bool acceptCommand, bool transferEnabled)
		{
			const uint64_t c = cycle++;
			acceptCommand = acceptCommand && c >= nextCommandCycle;
			uint64_t stage1 = c - cycleStartNext;

			if (acceptCommand && !activeNext && (field & 0x18))
			{
				activeNext = true; shortNext = false; modeNext = field; cycleStartNext = c;
				nextCommandCycle = c + commandSpacing();
				immOffsetAccNext = 0; varOffsetNext = 0; stage1 = 0;
			}
			else if (activeNext && stage1 <= 4 && stage1 > 0)
			{
				immOffsetAccNext += uint32_t(field) << ((stage1 - 1) * 5);
				const uint32_t m = (0x1fu << ((stage1 - 1) * 5)) & 0x3ffff;
				varOffsetNext |= (uint32_t(varOffset) >> 12) & m;
			}
			if (activeNext && stage1 == 4 && acceptCommand && (field & 0x18)) deferredCommand = field;
			if (extendedSchedule() && activeCurrent) tickExtendedTransfer(c, transferEnabled);

			if (!extendedSchedule() && compress && activeCurrent && c + 1 == cycleCommit)
			{
				if ((modeCurrent & 0x18) == 0x10)
				{
					if (transferEnabled && !shortCurrent) writePhase(lowEffectiveAddr, 0, writeLatchNext);
					writePackedNext = uint16_t((writePackedNext & 0xff) | (packEram(writeLatch) & 0xff00));
					setReadPacked(packEram(writeLatch));
				}
				else setReadPacked(uint16_t((readPackedNext & 0xff) * 0x101));
			}
			else if (!extendedSchedule() && rawSplit() && activeCurrent && c + 1 == cycleCommit)
			{
				if ((modeCurrent & 0x18) == 0x10)
				{
					if (transferEnabled && !shortCurrent) writePhase(lowEffectiveAddr, 0, writeLatchNext);
					readLatch = se<24>(writeLatch);
				}
				else readLatch = se<24>((readRawNext & 0xfff) * 0x1001);
			}
			if (!extendedSchedule() && rawSingle() && activeCurrent && c == cycleCommit)
			{
				if ((modeCurrent & 0x18) == 0x10) readLatch = se<24>(writeLatch);
				activeCurrent = false;
			}

			if (!extendedSchedule() && activeCurrent && c == cycleCommit)
			{
				if ((modeCurrent & 0x18) == 0x10)
				{
					if (rawSplit())
					{
						if (transferEnabled)
						{
							writePhase(effectiveAddr, 1, writeLatch);
							readLatch = se<24>(int32_t((uint32_t(readLatch) & 0xfff) | (uint32_t(writeLatch) & 0xfff000)));
						}
					}
					else if (!compress) writePhase(effectiveAddr, 0, writeLatchNext);
					else if (transferEnabled)
					{
						writePackedNext = uint16_t((writePackedNext & 0xff) | (packEram(writeLatch) & 0xff00));
						writePhase(effectiveAddr, 1, writeLatch);
						setReadPacked(uint16_t((readPacked & 0xff) | (writePackedNext & 0xff00)));
					}
				}
				else
				{
					if (compress) { if (transferEnabled) setReadPacked(uint16_t((readPackedAt(effectiveAddr) & 0xff00) | (readPackedNext & 0xff))); }
					else if (rawSplit()) { if (transferEnabled) readLatch = se<24>(int32_t((uint32_t(readRaw(effectiveAddr)) & 0xfff000) | (uint32_t(readRawNext) & 0xfff))); }
					else readLatch = se<24>(readRaw(effectiveAddr));
				}
				activeCurrent = false;
			}

			if (activeNext && stage1 == 5)
			{
				// Distance to the preceding transaction, for the early-read rule below.
				const uint64_t previousGap = cycleStartNext - cycleStartCurrent;
				activeCurrent = true;
				modeCurrent = modeNext; shortCurrent = shortNext;
				cycleStartCurrent = cycleStartNext;
				cycleCommit = cycleStartNext + (rawSingle() ? ERAM_COMMIT_STAGE_RAW_SINGLE : ERAM_COMMIT_STAGE);
				activeNext = false;
				writeLatchNext = writeLatch;
				writePackedNext = packEram(writeLatch);

				const uint32_t previousAddress = effectiveAddr;
				effectiveAddr = pos + (immOffsetAccNext & 0x3ffff);
				if ((modeNext & 0x18) == 0x18)
				{
					uint32_t positionMask = 0;
					for (unsigned bit = 0; bit < 20; bit += 5)
						if (immOffsetAccNext & (1u << bit)) positionMask |= 0x1fu << bit;
					effectiveAddr = (pos & ~positionMask) + varOffsetNext + ((immOffsetAccNext >> 1) & 1);
					highOffset = immOffsetAccNext & 0x100;
				}
				if (highOffset) effectiveAddr += 0x40000;
				effectiveAddr &= ERAM_MASK_FULL;
				lowEffectiveAddr = transferEnabled ? effectiveAddr : ((previousAddress & ~0x1ffu) | (effectiveAddr & 0x1ff));
				if (extendedSchedule()) tickExtendedTransfer(c, transferEnabled);
				else if (rawSingle())
				{
					const bool write = (modeCurrent & 0x18) == 0x10;
					if (transferEnabled)
					{
						if (write) writePhase(effectiveAddr, 0, writeLatchNext);
						readLatch = se<24>(write ? writeLatchNext : readRaw(effectiveAddr));
					}
					if (!write) activeCurrent = false;
				}
				else if (compress && (modeCurrent & 0x18) == 0x10)
					setReadPacked(uint16_t((readPacked & 0xff00) | (writePackedNext & 0xff)));
				else if (compress)
				{
					readPackedNext = readPackedAt(lowEffectiveAddr);
					if (shortCurrent) readPackedNext = uint16_t((readPackedNext >> 8) * 0x101);
					// Unmeasured: within ERAM_EARLY_READ_HOLD_GAP of the preceding command
					// the latch keeps the previous word (a 2-voice pitch shifter program relies on it).
					if (previousGap > ERAM_EARLY_READ_HOLD_GAP || shortCurrent)
						setReadPacked(uint16_t((readPacked & 0xff00) | (readPackedNext & 0xff)));
				}
				else if (rawSplit())
				{
					const bool write = (modeCurrent & 0x18) == 0x10;
					readRawNext = write ? writeLatchNext : readRaw(lowEffectiveAddr);
					readLatch = se<24>(int32_t((uint32_t(readLatch) & 0xfff000) | (uint32_t(readRawNext) & 0xfff)));
				}
				if (deferredCommand || (acceptCommand && (field & 0x18)))
				{
					activeNext = true; shortNext = true;
					modeNext = deferredCommand ? deferredCommand : field;
					cycleStartNext = c - (deferredCommand ? 1 : 0);
					nextCommandCycle = cycleStartNext + commandSpacing();
					immOffsetAccNext = deferredCommand ? field : 0;
					varOffsetNext = deferredCommand ? (uint32_t(varOffset) >> 12) & 31 : 0;
					deferredCommand = 0;
				}
			}
		}
	};

	template <int lg2eramSize>
	struct Runtime
	{
		CoreRuntime core[2];
		int32_t gram[RING_SIZE] = {};	// shared, rotates with core 0
		int32_t mulcoeffs[8] = {};
		uint8_t mailbox[4] = {};	// opcode 34 sub-op A export / host read select
		EramRuntime<lg2eramSize> eram;
		// Frame-level IRQ state.
		bool irq1History = false, irq1Request = false;
		uint8_t publishedDint = 2;	// bit0 IRQ0 level, bit1 IRQ1 level (idle high)
		uint32_t frameCycle = 0;
		bool framePaused = false;

		void reset()
		{
			core[0].reset(); core[1].reset();
			std::memset(gram, 0, sizeof(gram));
			std::memset(mulcoeffs, 0, sizeof(mulcoeffs));
			std::memset(mailbox, 0, sizeof(mailbox));
			eram.reset();
			irq1History = irq1Request = false;
			publishedDint = 2;
			frameCycle = 0;
			framePaused = false;
		}
		int32_t readGram(uint32_t offset) const { return gram[core[0].ringIndex(offset)]; }
		void writeGram(int32_t value, uint32_t offset) { gram[core[0].ringIndex(offset)] = value; }
		// Both cores' positions change together; GRAM follows core 0.
		void syncCores(bool advanceMemory)
		{
			const uint32_t before = core[0].iramPos;
			core[0].sync(advanceMemory); core[1].sync(advanceMemory);
			relocateRing(gram, before, core[0].iramPos);
		}
		void freezeCores()
		{
			const uint32_t before = core[0].iramPos;
			core[0].freeze(); core[1].freeze();
			relocateRing(gram, before, core[0].iramPos);
		}
	};
}
