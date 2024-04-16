#include "frontpanelState.h"

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/peripherals.h"

namespace virusLib
{
	void FrontpanelState::updateLfoPhaseFromTimer(dsp56k::DSP& _dsp, const uint32_t _lfo, const uint32_t _timer, const float _minimumValue/* = 0.0f*/, float _maximumValue/* = 1.0f*/)
	{
		updatePhaseFromTimer(m_lfoPhases[_lfo], _dsp, _timer, _minimumValue, _maximumValue);
	}

	void FrontpanelState::updatePhaseFromTimer(float& _target, dsp56k::DSP& _dsp, uint32_t _timer, float _minimumValue, float _maximumValue)
	{
		const auto* peripherals = _dsp.getPeriph(0);

		if(const auto* p362 = dynamic_cast<const dsp56k::Peripherals56362*>(peripherals))
		{
			const auto& t = p362->getTimers();
			updatePhaseFromTimer(_target, t, _timer, _minimumValue, _maximumValue);
		}
		else if(const auto* p303 = dynamic_cast<const dsp56k::Peripherals56303*>(peripherals))
		{
			const auto& t = p303->getTimers();
			updatePhaseFromTimer(_target, t, _timer, _minimumValue, _maximumValue);
		}
	}

	void FrontpanelState::updatePhaseFromTimer(float& _target, const dsp56k::Timers& _timers, uint32_t _timer, float _minimumValue, float _maximumValue)
	{
		const auto compare = _timers.readTCPR(static_cast<int>(_timer));
		const auto load = _timers.readTLR(static_cast<int>(_timer));

		const auto range = 0xffffff - load;

		const auto normalized = static_cast<float>(compare - load) / static_cast<float>(range);

		// the minimum PWM value is not always zero, we need to remap
		const auto floatRange = _maximumValue - _minimumValue;
		const auto floatRangeInv = 1.0f / floatRange;

		_target = (normalized - _minimumValue) * floatRangeInv;
	}
}
