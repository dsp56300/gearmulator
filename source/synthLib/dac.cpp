#include "dac.h"

#include "dsp56kEmu/logging.h"

namespace synthLib
{
	Dac::Dac() : m_processFunc(&DacProcessor<24, 0>::processSample)
	{
	}

	bool Dac::configure(const uint32_t _outputBits, const uint32_t _noiseBits)
	{
		auto processFunc = findProcessFunc(_outputBits, _noiseBits);

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

	Dac::ProcessFunc Dac::findProcessFunc(const uint32_t _outputBits, const uint32_t _noiseBits)
	{
		switch (_outputBits)
		{
		case 8:
			switch (_noiseBits)
			{
				case 0: return &DacProcessor<8, 0>::processSample;
				case 1: return &DacProcessor<8, 1>::processSample;
				case 2: return &DacProcessor<8, 2>::processSample;
				case 3: return &DacProcessor<8, 3>::processSample;
				case 4: return &DacProcessor<8, 4>::processSample;
				case 5: return &DacProcessor<8, 5>::processSample;
				case 6: return &DacProcessor<8, 6>::processSample;
				case 7: return &DacProcessor<8, 7>::processSample;
				default: return nullptr;
			}
		case 12:
			switch (_noiseBits)
			{
				case 0: return &DacProcessor<12, 0>::processSample;
				case 1: return &DacProcessor<12, 1>::processSample;
				case 2: return &DacProcessor<12, 2>::processSample;
				case 3: return &DacProcessor<12, 3>::processSample;
				case 4: return &DacProcessor<12, 4>::processSample;
				case 5: return &DacProcessor<12, 5>::processSample;
				case 6: return &DacProcessor<12, 6>::processSample;
				case 7: return &DacProcessor<12, 7>::processSample;
				default: return nullptr;
			}
		case 16:
			switch (_noiseBits)
			{
				case 0: return &DacProcessor<16, 0>::processSample;
				case 1: return &DacProcessor<16, 1>::processSample;
				case 2: return &DacProcessor<16, 2>::processSample;
				case 3: return &DacProcessor<16, 3>::processSample;
				case 4: return &DacProcessor<16, 4>::processSample;
				case 5: return &DacProcessor<16, 5>::processSample;
				case 6: return &DacProcessor<16, 6>::processSample;
				case 7: return &DacProcessor<16, 7>::processSample;
				default: return nullptr;
			}
		case 18:
			switch (_noiseBits)
			{
				case 0: return &DacProcessor<18, 0>::processSample;
				case 1: return &DacProcessor<18, 1>::processSample;
				case 2: return &DacProcessor<18, 2>::processSample;
				case 3: return &DacProcessor<18, 3>::processSample;
				case 4: return &DacProcessor<18, 4>::processSample;
				case 5: return &DacProcessor<18, 5>::processSample;
				case 6: return &DacProcessor<18, 6>::processSample;
				case 7: return &DacProcessor<18, 7>::processSample;
				default: return nullptr;
			}
		case 24:
			switch (_noiseBits)
			{
				case 0: return &DacProcessor<24, 0>::processSample;
				case 1: return &DacProcessor<24, 1>::processSample;
				case 2: return &DacProcessor<24, 2>::processSample;
				case 3: return &DacProcessor<24, 3>::processSample;
				case 4: return &DacProcessor<24, 4>::processSample;
				case 5: return &DacProcessor<24, 5>::processSample;
				case 6: return &DacProcessor<24, 6>::processSample;
				case 7: return &DacProcessor<24, 7>::processSample;
				default: return nullptr;
			}
		default:
			return nullptr;
		}
	}
}
