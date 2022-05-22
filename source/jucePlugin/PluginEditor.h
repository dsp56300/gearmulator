#pragma once

#include "VirusParameterBinding.h"

#include "PluginProcessor.h"

//==============================================================================
class AudioPluginAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
	void mouseDown(const juce::MouseEvent& event) override;

private:
	struct Skin
	{
		std::string displayName;
		std::string jsonFilename;
		std::string folder;

		bool operator == (const Skin& _other) const
		{
			return displayName == _other.displayName && jsonFilename == _other.jsonFilename && folder == _other.folder;
		}
	};

	void timerCallback() override;
	void loadSkin(const Skin& _skin);
	void setGuiScale(int percent);
	void openMenu();
	void exportCurrentSkin() const;
	Skin readSkinFromConfig() const;
	void writeSkinToConfig(const Skin& _skin) const;
	void setLatencyBlocks(uint32_t _blocks) const;

	// This reference is provided as a quick way for your editor to
	// access the processor object that created it.
	AudioPluginAudioProcessor& processorRef;

	VirusParameterBinding m_parameterBinding;

	std::unique_ptr<juce::Component> m_virusEditor;
	Skin m_currentSkin;
	float m_rootScale = 1.0f;

	std::vector<Skin> m_includedSkins;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessorEditor)
};
