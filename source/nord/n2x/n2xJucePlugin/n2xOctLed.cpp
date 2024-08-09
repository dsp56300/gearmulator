#include "n2xOctLed.h"

#include "n2xController.h"

namespace n2xJucePlugin
{
	OctLed::OctLed(Editor& _editor) : ParameterDrivenLed(_editor, "O2Pitch_LED", "O2Pitch")
	{
		disableClick();
		bind();
	}

	bool OctLed::updateToggleState(const pluginLib::Parameter* _parameter) const
	{
		const auto v = _parameter->getUnnormalizedValue();
		const bool active = v / 12 * 12 == v;
		return active;
	}
}
