#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace jucePluginEditorLib
{
	class PluginEditorState;

	//==============================================================================
	class EditorWindow : public juce::AudioProcessorEditor, juce::Timer
	{
	public:
	    explicit EditorWindow (juce::AudioProcessor& _p, PluginEditorState& _s, juce::PropertiesFile& _config);
	    ~EditorWindow() override;

		void paint(juce::Graphics& g) override {}

		void resized() override;

		int getControlParameterIndex(Component&) override;

	private:
		void setGuiScale(float _percent);
		void setUiRoot(juce::Component* _component);
		void updateSizeConstrainer();
		void onSkinSizeChanged();

		juce::Point<int> getSizeForScale(float _percent) const;
		juce::Point<int> getSizeForSkinScale(float _scale) const;
		bool isMinimumSize(int _width, int _height) const;

		void timerCallback() override;
		void fixParentWindowSize() const;

		PluginEditorState& m_state;
		juce::PropertiesFile& m_config;

	    juce::ComponentBoundsConstrainer m_sizeConstrainer;
		// editor pixels per skin pixel, which is the GUI scale in percent times the skin's root scale
		float m_skinScale = 0.0f;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorWindow)
	};
}
