#pragma once

// Program-side state of one ESP: the host aperture (program RAM of both cores),
// the eight configuration bytes, host transaction staging and the decode cache.

#include "esp_runtime.h"

#include <functional>

namespace esp
{
	template <int lg2eramSize>
	class Base
	{
	public:
		using RuntimeT = Runtime<lg2eramSize>;
		using ReceiveInput = std::function<int32_t(uint8_t)>;
		using DintCallback = std::function<void(uint32_t, uint8_t, uint8_t)>;

		explicit Base(RuntimeT* shared = nullptr)
			: m_ownRuntime(shared ? nullptr : std::make_unique<RuntimeT>()), m_rt(shared ? shared : m_ownRuntime.get())
		{
			reset();
		}
		Base(const Base&) = delete;
		Base& operator=(const Base&) = delete;

		RuntimeT& runtime() { return *m_rt; }
		const RuntimeT& runtime() const { return *m_rt; }
		// Bumped by every host write that changes what a native kernel bakes in:
		// program structure or the ERAM command schedule. Coefficients are live data.
		uint64_t structureGeneration() const { return m_structureGeneration; }

		// Entry contract for a native frame (the dispatcher also checks the ERAM state).
		bool batchedFrameReady() const
		{
			const auto& rt = runtime();
			if (rt.frameCycle || hasReceiveInput() || !dspClockRunning() ||
				!coreRunning(0) || !coreRunning(1) || (config(2) & 0x40) || (config(5) & 0x60)) return false;
			for (const auto& core : rt.core)
			{
				if (core.pc != 0 || core.pcjumpat >= 0 || core.accA.head != 0 || core.accB.head != 0 ||
					core.accA.outputClear || core.accB.outputClear || core.skipfieldPos || core.skipfieldNeg ||
					core.predicateReady || core.comparePending) return false;
			}
			return rt.core[0].iramPos == rt.core[1].iramPos;
		}

		void reset()
		{
			std::memset(words, 0, sizeof(words));
			for (auto& core : instr) for (auto& i : core) i = Instr();
			std::memset(staging, 0, sizeof(staging));
			ifMode = kModeProgramWrite;
			// Emulator startup profile, not a claim about power-on register values.
			const uint8_t startup[8] = {0xaa, 0, 0x90, 0x54, 0xc7, 0, 0, 0};
			std::memcpy(configuration, startup, 8);
			hostTailAddress[0] = hostTailAddress[1] = -1;
			iramInhibit = 0;
			conditionalBranches = true;
			irq0SelectCore1 = irq1SelectCore1 = false;
			++m_structureGeneration;
			for (auto& tables : m_tables)
			{
				std::memset(tables.coef16, 0, sizeof(tables.coef16));
				std::memset(tables.shift, 7, sizeof(tables.shift));
			}
			std::memset(m_eramImm, 0, sizeof(m_eramImm));
			m_rt->reset();
			m_rt->eram.configureMemory(configuration[3]);
			m_rt->eram.configureBus(configuration[5]);
			m_scheduleHashValid = m_scheduleDirty = false;
		}

		// Host interface: byte bus, 14-bit address.
		void hostWrite(uint32_t address, uint8_t value)
		{
			address &= 0x3fff;
			const bool enteringRotationHold = address == 0x2001 && (value & 0x40) && !(configuration[1] & 0x40);
			if (address >= 0x2000 && address <= 0x2007)
			{
				configuration[address - 0x2000] = value;
			}
			if (address == 0x2005)
			{
				iramInhibit = (value >> 5) & 3;
				for (unsigned c = 0; c < 2; ++c)
				{
					const bool clear = iramInhibit & (1 << c);
					m_rt->core[c].accA.outputClear = clear;
					m_rt->core[c].accB.outputClear = clear;
					if (clear) std::memset(m_rt->core[c].iram, 0, sizeof(m_rt->core[c].iram));
				}
				m_rt->eram.configureBus(value);
			}
			if (address == 0x2001)
			{
				if (enteringRotationHold)
				{
					m_rt->freezeCores();
					m_rt->eram.freeze();
				}
				irq0SelectCore1 = (value & 0x10) != 0;
				irq1SelectCore1 = (value & 0x20) != 0;
				return;
			}
			if (address == 0x2003)
			{
				ifMode = value & 3;
				conditionalBranches = (value & 0x10) != 0;
				m_rt->eram.configureMemory(value);
				// The low mode bits change with almost every coefficient transaction;
				// only the command spacing affects the ERAM schedule.
				if (!m_scheduleHashValid || m_scheduleSpacing != m_rt->eram.commandSpacing())
					cacheScheduleHash();
				return;
			}
			if (address >= 0x2000) return;
			staging[address & 3] = value;
			if ((address & 3) != 3) return;
			commitHostWord(address);
			flushSchedule();
		}

		uint8_t hostRead(uint32_t address) const { return uint8_t(readHostReg() >> ((address & 3) * 8)); }

		uint32_t readHostReg() const
		{
			// 2004 bits 4/5 expose the running core's delayed saturated A.
			if ((configuration[4] & 0x30) && ifMode != kModeProgramWrite)
			{
				const uint32_t value = uint32_t(configuration[4] & 0x20 ? m_rt->core[1].accA.pipelineSat24() : m_rt->core[0].accA.pipelineSat24());
				return ifMode == kModeCoefWrite ? value & ~0x3ffu : value;
			}
			uint32_t v;
			std::memcpy(&v, m_rt->mailbox, 4);
			return v;
		}

		// Direct program access (tests, tools, boards without a host bus).
		void writePMem(uint16_t byteAddress, uint8_t value)
		{
			if ((byteAddress & 3) == 3) value &= byteAddress < 0x1000 ? 0 : 0x0f;
			const unsigned index = byteAddress >> 2;
			const uint32_t shift = (byteAddress & 3) * 8;
			const uint32_t word = wordAt(index);
			setWord(index, (word & ~(0xffu << shift)) | (uint32_t(value) << shift));
			flushSchedule();
		}
		void writePMem32(uint16_t wordIndex, uint32_t value) { setWord(wordIndex, value); flushSchedule(); }
		uint8_t readPRAM(size_t byteOffset) const { return uint8_t(wordAt(unsigned(byteOffset >> 2)) >> ((byteOffset & 3) * 8)); }
		int32_t hostTail(unsigned core) const { return hostTailAddress[core & 1]; }
		// Live data for compiled engines: the coefficient and shift of every word (LiveTables)
		// and the 20-bit ERAM immediate assembled from the four words after a command.
		const int16_t* coefficientTable(unsigned core) const { return m_tables[core & 1].coef16; }
		const uint8_t* shiftTable(unsigned core) const { return m_tables[core & 1].shift; }
		const uint32_t* eramImmediateTable() const { return m_eramImm; }
		const uint32_t* wordsOf(unsigned core) const { return words[core & 1]; }
		uint32_t wordAt(unsigned index) const { return words[(index >> 10) & 1][index & 0x3ff]; }
		const Instr& instruction(unsigned core, unsigned pc) const { return instr[core][pc]; }
		uint8_t config(unsigned index) const { return configuration[index & 7]; }

		void setDintCallback(DintCallback callback) { onDint = std::move(callback); }
		void setDintFrameCycles(uint32_t cycles) { dintFrameCycles = cycles; }
		void setExternalClockPresent(bool present) { externalClockPresent = present; }
		void setReceiveInput(ReceiveInput input) { receiveInput = std::move(input); }
		// Initial codec selection, until the host writes 2003.
		void setEramCompression(bool compress) { m_rt->eram.setCompression(compress); }
		bool isEramCompression() const { return m_rt->eram.compress; }
		bool dspClockRunning() const { return externalClockPresent || (configuration[2] & 0x80); }
		bool hasReceiveInput() const { return static_cast<bool>(receiveInput); }
		bool conditionalBranchesEnabled() const { return conditionalBranches; }
		bool coreRunning(unsigned core) const
		{
			return (configuration[4] & (0x40 << core)) && !m_rt->framePaused && (core || !(configuration[2] & 1));
		}

	protected:
		// Host aperture: 1024 words per core, the first 768 executable. A branch can
		// target 768..1023 and that word executes once before the PC runs out.
		uint32_t words[2][PRAM_WORDS] = {};
		Instr instr[2][PRAM_WORDS];
		uint8_t configuration[8] = {};
		uint8_t ifMode = kModeProgramWrite;
		uint8_t staging[4] = {};
		int32_t hostTailAddress[2] = {-1, -1};
		bool externalClockPresent = false;
		uint8_t iramInhibit = 0;
		bool conditionalBranches = true;
		bool irq0SelectCore1 = false, irq1SelectCore1 = false;
		uint32_t dintFrameCycles = FRAME_CLOCKS;
		ReceiveInput receiveInput;
		DintCallback onDint;
		uint64_t m_structureGeneration = 0;
		LiveTables m_tables[2] = {};
		uint32_t m_eramImm[PRAM_WORDS] = {};
		uint64_t m_scheduleHash = 0;
		unsigned m_scheduleSpacing = 0;
		bool m_scheduleHashValid = false, m_scheduleDirty = false;

		uint64_t cacheScheduleHash()
		{
			m_scheduleSpacing = m_rt->eram.commandSpacing();
			m_scheduleHashValid = true;
			return m_scheduleHash = scheduleHash();
		}

		static int16_t coefficient16(const Instr& in) { return int16_t(int32_t(in.coef) * (1 << (7 - in.shift))); }
		void refreshEramImmediate(unsigned pc)
		{
			// Chunks at word indexes 756 and beyond never reach the controller.
			uint32_t imm = 0;
			for (unsigned k = 1; k <= 4; ++k)
				if (pc + k < NORMAL_DECODE_END) imm |= ((words[1][pc + k] >> 23) & 31) << (5 * (k - 1));
			m_eramImm[pc] = imm;
		}
		// Which words start ERAM transactions, with what mode, and whether a command
		// is deferred: the controller's control flow over the static field stream
		// (CoreProgram::lowerEram mirrors it). A field edit that changes this is
		// structural for a native kernel; any other field edit is data.
		uint64_t scheduleHash() const
		{
			const unsigned spacing = m_rt->eram.commandSpacing();
			bool activeNext = false;
			int64_t cycleStartNext = 0, nextCommandCycle = 0;
			uint8_t deferredCommand = 0;
			uint64_t h = 1469598103934665603ull;
			auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
			for (uint32_t c = 0; c < FRAME_CLOCKS; ++c)
			{
				const uint8_t field = c < NORMAL_DECODE_END ? (words[1][c] >> 23) & 31 : 0;
				const bool acceptCommand = c < 755 && int64_t(c) >= nextCommandCycle;
				int64_t stage1 = int64_t(c) - cycleStartNext;
				if (acceptCommand && !activeNext && (field & 0x18))
				{
					activeNext = true; cycleStartNext = c; nextCommandCycle = c + spacing; stage1 = 0;
					mix(c); mix(field);
				}
				if (activeNext && stage1 == 4 && acceptCommand && (field & 0x18)) deferredCommand = field;
				if (activeNext && stage1 == 5)
				{
					activeNext = false;
					if (deferredCommand || (acceptCommand && (field & 0x18)))
					{
						const uint8_t mode = deferredCommand ? deferredCommand : field;
						activeNext = true;
						cycleStartNext = int64_t(c) - (deferredCommand ? 1 : 0);
						nextCommandCycle = cycleStartNext + spacing;
						mix(uint64_t(cycleStartNext)); mix(mode); mix(deferredCommand ? 1 : 2);
						deferredCommand = 0;
					}
				}
			}
			return h;
		}

		void setWord(unsigned index, uint32_t value)
		{
			const unsigned core = (index >> 10) & 1, pc = index & 0x3ff;
			value &= programWordMask(index);
			uint32_t& word = words[core][pc];
			if (word == value) return;
			const uint32_t before = word;
			// Kernels specialize on which words carry ERAM commands, reachable or not.
			if (core == 1 && pc < PRAM_SIZE && ((before ^ value) & 0x0c000000u)) ++m_structureGeneration;
			word = value;
			instr[core][pc].decode(value);
			m_tables[core].coef16[pc] = coefficient16(instr[core][pc]);
			m_tables[core].shift[pc] = instr[core][pc].shift;
			if (structureOf(before) != structureOf(value)) ++m_structureGeneration;
			if (core == 1 && ((before ^ value) >> 23))
			{
				for (unsigned k = 1; k <= 4 && k <= pc; ++k) refreshEramImmediate(pc - k);
				m_scheduleDirty = true;
			}
		}

		void flushSchedule()
		{
			if (!m_scheduleDirty) return;
			m_scheduleDirty = false;
			const uint64_t before = m_scheduleHash;
			if (cacheScheduleHash() != before) ++m_structureGeneration;
		}

		void commitHostWord(uint32_t address)
		{
			const unsigned addr = address >> 2;
			// Byte 3 commits the selection in every mode; 2003.bit3 also selects
			// the other core's host-tail window, including read select.
			hostTailAddress[(addr >> 10) & 1] = int32_t(addr & 0x3ff);
			if (configuration[3] & 8) hostTailAddress[1 - ((addr >> 10) & 1)] = int32_t(addr & 0x3ff);
			if (ifMode == kModeReadSelect)
			{
				const uint32_t w = wordAt(addr);
				std::memcpy(m_rt->mailbox, &w, 4);
				return;
			}
			const unsigned count = ifMode == kModeProgramWrite ? 1 : ifMode == kModeCoefWrite ? 2 : 5;
			if (addr + count > 2 * PRAM_WORDS) return;
			const auto wordAddress = [addr](unsigned offset)
			{
				// Packed writes stop advancing at the last executable word.
				const unsigned pc = addr & 0x3ff;
				return pc < PRAM_SIZE ? (addr & 0x400) | (pc + offset < PRAM_SIZE - 1 ? pc + offset : PRAM_SIZE - 1) : addr + offset;
			};
			uint32_t w[5] = {};
			for (unsigned i = 0; i < count; ++i) w[i] = wordAt(wordAddress(i));
			const uint8_t* b = staging;
			if (ifMode == kModeProgramWrite)
				w[0] = uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
			else if (ifMode == kModeCoefWrite)
			{
				w[0] = (w[0] & ~0x3ffu) | b[0] | ((b[1] & 3u) << 8);
				w[1] = (w[1] & ~0x3ffu) | (b[1] >> 2) | ((b[2] & 15u) << 6);
			}
			else
			{
				const uint32_t fields[5] = {uint32_t(b[0] >> 3), b[1] & 31u, uint32_t(b[1] >> 5) | ((b[2] & 3u) << 3),
					(b[2] >> 2) & 31u, uint32_t(b[2] >> 7) | ((b[3] & 15u) << 1)};
				for (unsigned i = 0; i < count; ++i) w[i] = (w[i] & ~(31u << 23)) | (fields[i] << 23);
			}
			// 2003.bit3 broadcasts the addressed core's read/modify/write result.
			const bool broadcast = (configuration[3] & 8) != 0;
			for (unsigned copy = 0; copy < (broadcast ? 2u : 1u); ++copy)
				for (unsigned i = 0; i < count; ++i)
					setWord(wordAddress(i) ^ (copy ? 0x400u : 0), w[i]);
		}

	private:
		std::unique_ptr<RuntimeT> m_ownRuntime;
		RuntimeT* m_rt;
	};
}
