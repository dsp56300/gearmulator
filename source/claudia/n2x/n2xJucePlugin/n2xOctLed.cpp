#include "n2xOctLed.h"

#include "n2xController.h"

namespace n2xJucePlugin
{
	OctLed::OctLed(Editor& _editor) : ParameterDrivenLed(_editor, "O2Pitch_LED", "O2Pitch")
	{
		bind();
	}

	bool OctLed::updateToggleState(const pluginLib::Parameter* _parameter) const
	{
		const auto v = _parameter->getUnnormalizedValue();
		const bool active = v / 12 * 12 == v;
		return active;
	}

	void OctLed::onClick(pluginLib::Parameter* _targetParameter, bool _toggleState)
	{
		const auto v = _targetParameter->getUnnormalizedValue();
		auto newV = v + 6;
		newV /= 12;
		newV *= 12;
		if(newV != v)
			_targetParameter->setUnnormalizedValueNotifyingHost(newV, pluginLib::Parameter::Origin::Ui);
	}
}
