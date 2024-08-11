#pragma once

#include "n2xParameterDrivenLed.h"

namespace n2xJucePlugin
{
	class OctLed : ParameterDrivenLed
	{
	public:
		explicit OctLed(Editor& _editor);
	protected:
		bool updateToggleState(const pluginLib::Parameter* _parameter) const override;
		void onClick(pluginLib::Parameter* _targetParameter, bool _toggleState) override;
	};
}
