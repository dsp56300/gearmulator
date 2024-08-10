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

		static std::string getSyncMultiParamName(uint8_t _part, uint8_t _lfoIndex);

	private:
		void bind() const;
		void updateState(const pluginLib::Parameter* _param) const;

		Editor& m_editor;
		const uint8_t m_index;
		juce::Slider* m_slider;

		pluginLib::EventListener<uint8_t> m_onCurrentPartChanged;
	};
}
