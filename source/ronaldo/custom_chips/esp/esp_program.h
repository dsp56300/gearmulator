#pragma once

#include "esp_base.h"

#include <vector>

namespace esp
{
	// What a compile reads: the program words and the ERAM format bits. Taken on
	// the audio thread and decoded by the compiler; the live tables (coefficients,
	// immediates) are read at run time.
	struct ProgramSnapshot
	{
		uint32_t words[2][PRAM_WORDS];
		uint8_t memoryMode = 0x40;
		bool compress = true, staticBus = false;
		bool conditionalBranches = true;
		unsigned spacing = 5;

		template <int lg2eramSize> static ProgramSnapshot take(const Base<lg2eramSize>& base)
		{
			ProgramSnapshot s;
			s.capture(base);
			return s;
		}
		template <int lg2eramSize> void capture(const Base<lg2eramSize>& base)
		{
			for (unsigned c = 0; c < 2; ++c)
				std::memcpy(words[c], base.wordsOf(c), sizeof(words[c]));
			const auto& e = base.runtime().eram;
			memoryMode = e.memoryMode;
			compress = e.compress;
			staticBus = e.staticBus;
			spacing = e.commandSpacing();
			conditionalBranches = base.conditionalBranchesEnabled();
			m_instr.clear();
		}
		void decode()
		{
			m_instr.resize(2 * PRAM_WORDS);
			for (unsigned c = 0; c < 2; ++c)
				for (unsigned i = 0; i < PRAM_WORDS; ++i)
					m_instr[c * PRAM_WORDS + i].decode(words[c][i]);
		}
		bool rawSplit() const { return !compress && memoryMode == 0x20; }
		bool rawSingle() const { return !compress && memoryMode == 0; }
		bool extended() const { return staticBus || memoryMode == 0x60; }
		const Instr& instruction(unsigned c, unsigned pc) const { return m_instr[c * PRAM_WORDS + pc]; }

	private:
		std::vector<Instr> m_instr;
	};
}
