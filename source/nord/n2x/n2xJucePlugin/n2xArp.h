#pragma once

#include "n2xParameterDrivenLed.h"

namespace n2xJucePlugin
{
	class Arp : ParameterDrivenLed
	{
	public:
		explicit Arp(Editor& _editor);

	protected:
		bool updateToggleState(const pluginLib::Parameter* _parameter) const override;
		void onClick(pluginLib::Parameter* _targetParameter, bool _toggleState) override;
	};
}
