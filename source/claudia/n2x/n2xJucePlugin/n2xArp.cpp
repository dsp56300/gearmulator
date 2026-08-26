#include "n2xArp.h"

#include "n2xController.h"

namespace n2xJucePlugin
{
	Arp::Arp(Editor& _editor) : ParameterDrivenLed(_editor, "ArpEnabled", "Lfo2Dest")
	{
		bind();
	}

	bool Arp::updateToggleState(const pluginLib::Parameter* _parameter) const
	{
		const auto v = _parameter->getUnnormalizedValue();
		const bool arpActive = v != 3 && v != 4 && v != 7;
		return arpActive;
	}

	void Arp::onClick(pluginLib::Parameter* _targetParameter, const bool _toggleState)
	{
		_targetParameter->setUnnormalizedValueNotifyingHost(_toggleState ? 0 : 3, pluginLib::Parameter::Origin::Ui);
	}
}
