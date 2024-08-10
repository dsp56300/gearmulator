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

	bool PartLed::updateToggleState(const pluginLib::Parameter* _parameter) const
	{
		return _parameter->getUnnormalizedValue() <= 15;
	}
}
