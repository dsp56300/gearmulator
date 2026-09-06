#pragma once

#include <unordered_map>
#include <vector>

#include "dsp56kEmu/types.h"

namespace dsp56k
{
	class DSP;
	struct JitConfig;
}

namespace wLib
{
	class Dsp
	{
	protected:
		void enableDynamicPeripheralAddressing(dsp56k::JitConfig& _config, dsp56k::DSP& _dsp, dsp56k::TWord _opA, dsp56k::TWord _opB, dsp56k::TWord _size);

	private:
		struct DynamicPeripheralPattern
		{
			dsp56k::TWord opA;
			dsp56k::TWord opB;
			dsp56k::TWord size;
		};

		std::vector<DynamicPeripheralPattern> m_dynamicPeripheralPatterns;
		std::unordered_map<dsp56k::TWord, dsp56k::TWord> m_dynamicPeripheralAddressingRanges;
	};
}
