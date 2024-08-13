#pragma once

#include "n2xParameterDrivenLed.h"

namespace n2xJucePlugin
{
	class PartLed : ParameterDrivenLed
	{
	public:
		PartLed(Editor& _editor, uint8_t _slot);

	protected:
		void updateState(juce::Button& _target, const pluginLib::Parameter* _source) const override;
		bool updateToggleState(const pluginLib::Parameter* _parameter) const override;

	private:
		const uint8_t m_slot;
	};
}
