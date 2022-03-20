#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace juce
{
	class Component;
}

namespace genericVirusUI
{
	class VirusEditor;

	class FxPage : public juce::Value::Listener
	{
	public:
		explicit FxPage(VirusEditor& _editor);
		~FxPage() override;

		void valueChanged(juce::Value& value) override;
	private:
		void updateReverbDelay() const;

		VirusEditor& m_editor;
		juce::Component* m_reverbContainer = nullptr;
		juce::Component* m_delayContainer = nullptr;
	};
}
