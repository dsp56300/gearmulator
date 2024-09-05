#pragma once

#include "jucePluginLib/event.h"

#include <vector>
#include <cstdint>

namespace juce
{
	class Button;
	class ComboBox;
	class Slider;
}

namespace xtJucePlugin
{
	class Editor;

	class Arp
	{
	public:
		explicit Arp(Editor& _editor);

	private:
		void bind();
		template<typename T> void bindT(T* _component, const char** _bindings);

		Editor& m_editor;

		juce::ComboBox* m_arpMode;
		juce::ComboBox* m_arpClock;
		juce::ComboBox* m_arpPattern;
		juce::ComboBox* m_arpDirection;
		juce::ComboBox* m_arpOrder;
		juce::ComboBox* m_arpVelocity;
		juce::Slider* m_arpTempo;
		juce::Slider* m_arpRange;
		std::vector<juce::Button*> m_arpReset;

		pluginLib::EventListener<bool> m_onPlayModeChanged;
		pluginLib::EventListener<uint8_t> m_onPartChanged;
	};
}
