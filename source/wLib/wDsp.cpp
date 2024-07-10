#include "wDsp.h"

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/jitconfig.h"

namespace wLib
{
	void Dsp::enableDynamicPeripheralAddressing(dsp56k::JitConfig& _config, dsp56k::DSP& _dsp, dsp56k::TWord _opA, dsp56k::TWord _opB, dsp56k::TWord _size)
	{
		// some startup code uses dynamic peripheral addressing, we allow this only for that code segment
		_config.getBlockConfig = [this, _opA, _opB, _size, &_dsp](const dsp56k::TWord _pc) -> std::optional<dsp56k::JitConfig>
		{
			for (const auto& [start, size] : m_dynamicPeripheralAddressingRanges)
			{
				if(_pc < start || _pc > start + size)
					continue;

				auto c = _dsp.getJit().getConfig();
				c.dynamicPeripheralAddressing = true;
				return c;
			}

			// find the op as specified by parameters
			// clr b M_AAR3,r2
			const auto opA = _dsp.memory().get(dsp56k::MemArea_P, _pc);
			const auto opB = _dsp.memory().get(dsp56k::MemArea_P, _pc + 1);

			if(opA == _opA && opB == _opB)
			{
				m_dynamicPeripheralAddressingRanges.insert({_pc, _size});
				auto c = _dsp.getJit().getConfig();
				c.dynamicPeripheralAddressing = true;
				return c;
			}

			return {};
		};
	}
}
