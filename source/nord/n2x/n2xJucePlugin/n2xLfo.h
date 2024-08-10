#pragma once

#include "n2xParameterDrivenLed.h"

namespace juce
{
	class Slider;
}

namespace n2xJucePlugin
{
	class Lfo
	{
	public:
		Lfo(Editor& _editor, uint8_t _index);

		static std::string getSyncMultiParamName(const uint8_t _part, const uint8_t _lfoIndex);

	private:
		void bind();
		void updateState(const pluginLib::Parameter* _param) const;

		Editor& m_editor;
		const uint8_t m_index;
		juce::Button* m_button;

		pluginLib::ParameterListener m_onSyncMultiParamChanged;
		pluginLib::ParameterListener m_onSyncRateParamChanged;
		pluginLib::EventListener<uint8_t> m_onCurrentPartChanged;
	};
}
