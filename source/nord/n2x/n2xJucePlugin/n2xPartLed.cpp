#include "n2xPartLed.h"

#include "jucePluginLib/parameter.h"

namespace n2xJucePlugin
{
	namespace
	{
		std::string getName(const uint8_t _slot)
		{
			return std::string("PerfMidiChannel") + static_cast<char>('A' + _slot);
		}
	}

	PartLed::PartLed(Editor& _editor, const uint8_t _slot) : ParameterDrivenLed(_editor, getName(_slot), getName(_slot), 0)
	, m_slot(_slot)
	{
		bind();
		disableClick();
	}

	void PartLed::updateState(juce::Button& _target, const pluginLib::Parameter* _source) const
	{
		ParameterDrivenLed::updateState(_target, _source);
		const auto ch = _source->getUnnormalizedValue();
		_target.setAlpha(ch > 0 && ch <= 15 ? 0.5f : 1.0f);
	}

	bool PartLed::updateToggleState(const pluginLib::Parameter* _parameter) const
	{
		return _parameter->getUnnormalizedValue() <= 15;
	}
}
