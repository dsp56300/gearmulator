#pragma once

#include <unordered_map>

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
		std::unordered_map<dsp56k::TWord, dsp56k::TWord> m_dynamicPeripheralAddressingRanges;
	};
}
