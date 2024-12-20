#pragma once

#include "baseLib/event.h"

#include <cstdint>

namespace n2x
{
	enum class KnobType;
}

namespace juce
{
	class Slider;
}

namespace n2xJucePlugin
{
	class Editor;

	class MasterVolume
	{
	public:
		explicit MasterVolume(const Editor& _editor);

	private:
		const Editor& m_editor;

		juce::Slider* m_volume;

		baseLib::EventListener<n2x::KnobType, uint8_t> m_onKnobChanged;
	};
}
