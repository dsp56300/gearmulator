#include "dac.h"

#include "dsp56kBase/logging.h"

namespace synthLib
{
	namespace
	{
		template<uint32_t OutputBits> Dac::ProcessFunc findProcessFunc(const uint32_t _noiseBits)
		{
			switch (_noiseBits)
			{
			case 0: return &DacProcessor<OutputBits, 0>::processSample;
			case 1: return &DacProcessor<OutputBits, 1>::processSample;
			case 2: return &DacProcessor<OutputBits, 2>::processSample;
			case 3: return &DacProcessor<OutputBits, 3>::processSample;
			case 4: return &DacProcessor<OutputBits, 4>::processSample;
			case 5: return &DacProcessor<OutputBits, 5>::processSample;
			case 6: return &DacProcessor<OutputBits, 6>::processSample;
			case 7: return &DacProcessor<OutputBits, 7>::processSample;
			default: return nullptr;
			}
		}

		Dac::ProcessFunc findProcessFunc(const uint32_t _outputBits, const uint32_t _noiseBits)
		{
			switch (_outputBits)
			{
			case 8:  return findProcessFunc<8>(_noiseBits);
			case 12: return findProcessFunc<12>(_noiseBits);
			case 16: return findProcessFunc<16>(_noiseBits);
			case 18: return findProcessFunc<18>(_noiseBits);
			case 24: return findProcessFunc<24>(_noiseBits);
			default: return nullptr;
			}
		}
	}

	Dac::Dac() : m_processFunc(&DacProcessor<24, 0>::processSample)
	{
	}

	bool Dac::configure(const uint32_t _outputBits, const uint32_t _noiseBits)
	{
		const auto processFunc = findProcessFunc(_outputBits, _noiseBits);

		if(processFunc == nullptr)
		{
			LOG("DAC configuration failed, unable to find process function for outputBits << " << _outputBits << " and noise bits " << _noiseBits);
			return false;
		}

		m_processFunc = processFunc;
		m_outputBits = _outputBits;
		m_noiseBits = _noiseBits;

		return true;
	}
}
