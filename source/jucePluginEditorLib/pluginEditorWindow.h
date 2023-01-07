#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class AudioPluginAudioProcessor;

namespace jucePluginEditorLib
{
	class PluginEditorState;

	//==============================================================================
	class EditorWindow : public juce::AudioProcessorEditor
	{
	public:
	    explicit EditorWindow (juce::AudioProcessor& _p, PluginEditorState& _s, juce::PropertiesFile& _config);
	    ~EditorWindow() override;

		void mouseDown(const juce::MouseEvent& event) override;

		void paint(juce::Graphics& g) override {}

	private:
		void setGuiScale(juce::Component* _component, int percent);
		void setUiRoot(juce::Component* _component);

		PluginEditorState& m_state;
		juce::PropertiesFile& m_config;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorWindow)
	};
}
