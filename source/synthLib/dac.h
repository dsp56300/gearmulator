#pragma once

#include <cmath>
#include <cstdint>

#include "dsp56kEmu/types.h"
#include "dsp56kEmu/utils.h"

namespace juce
{
	class Process;
}

namespace synthLib
{
	struct DacState
	{
		uint32_t randomValue = 56362;
	};

	namespace dacHelper
	{
		inline uint32_t lcg(uint32_t& _state)
		{
			// https://en.wikipedia.org/wiki/Linear_congruential_generator
			constexpr uint32_t a = 1664525;
			constexpr uint32_t c = 1013904223;
			constexpr uint32_t m = 0xffffffff;

			_state = (a * (_state) + c) & m;

			return _state;
		}
	}

	template<uint32_t OutputBits, uint32_t NoiseBits> class DacProcessor
	{
	public:
		static constexpr uint32_t InBits  = 24;

		// first noise bit is 0.5 bits so add one to the output bits
		static constexpr uint32_t OutBits = NoiseBits > 0 ? (OutputBits + 1) : OutputBits;

		static_assert(OutputBits > 0, "OutputBits must be > 0");
		static_assert(NoiseBits < OutBits, "NoiseBits must be <= OutputBits");

		static constexpr float FloatToIntScale = static_cast<float>(1 << (OutputBits-1));	// 1 bit sign
		static constexpr float IntToFloatScale = 1.0f / FloatToIntScale;

		static float processSample(DacState& _dacState, const dsp56k::TWord _in)
		{
			int32_t v = dsp56k::signextend<int32_t,24>(static_cast<int32_t>(_in));

			if constexpr (OutBits > InBits)
			{
				v <<= (OutBits - InBits);
			}
			else if constexpr (OutBits < InBits)
			{
				constexpr int32_t rounder = (1<<(InBits - OutBits-1)) - 1;
				v += rounder;
				v >>= (InBits - OutBits);
			}

//			v = 0;

			if constexpr (NoiseBits > 0)
			{
				constexpr int32_t rounder = (1<<(NoiseBits-1)) - 1;

				_dacState.randomValue = dacHelper::lcg(_dacState.randomValue);

				const int32_t randomValue = _dacState.randomValue >> (32u - NoiseBits);
				v += (randomValue-rounder);

				v >>= 1;
			}

			return static_cast<float>(v) * IntToFloatScale;
		}
	};

	class Dac
	{
	public:
		Dac();

		using ProcessFunc = float(*)(DacState&, dsp56k::TWord);

		bool configure(uint32_t _outputBits, uint32_t _noiseBits);

		float processSample(const dsp56k::TWord _in)
		{
			return m_processFunc(m_state, _in);
		}

	private:
		static ProcessFunc findProcessFunc(uint32_t _outputBits, uint32_t _noiseBits);

		ProcessFunc m_processFunc;
		DacState m_state;
		uint32_t m_outputBits = 24;
		uint32_t m_noiseBits = 1;
	};
}
