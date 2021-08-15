#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "version.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor &p) :
	AudioProcessorEditor(&p), processorRef(p), m_btSingleMode("Single Mode"), m_btMultiMode("Multi Mode"),
	m_tempEditor(p)
{
    ignoreUnused (processorRef);

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (400, 300);

	m_btSingleMode.setRadioGroupId(0x3cf);
	m_btMultiMode.setRadioGroupId(0x3cf);
	addAndMakeVisible(m_btSingleMode);
	addAndMakeVisible(m_btMultiMode);
	m_btSingleMode.setTopLeftPosition(0,0);
	m_btSingleMode.setSize(120,30);
	m_btMultiMode.getToggleStateValue().referTo(*processorRef.getController().getParam(0, 2, 0x7a));
	const auto isMulti = processorRef.getController().isMultiMode();
	m_btSingleMode.setToggleState(!isMulti, juce::dontSendNotification);
	m_btMultiMode.setToggleState(isMulti, juce::dontSendNotification);
	m_btSingleMode.setClickingTogglesState(true);
	m_btMultiMode.setClickingTogglesState(true);
	m_btMultiMode.setTopLeftPosition(m_btSingleMode.getPosition().x + m_btSingleMode.getWidth() + 10, 0);
	m_btMultiMode.setSize(120,30);

	addAndMakeVisible(m_tempEditor);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (15.0f);

	std::string message = "DSP 56300 Emulator\nVersion " + std::string(g_pluginVersionString) + "\n" __DATE__ " " __TIME__;

	if(!processorRef.isPluginValid())
		message += "\n\nNo ROM, no sound!\nCopy ROM next to plugin, must end with .bin";

	g.drawFittedText(message, getLocalBounds(), juce::Justification::centred, 2);
    g.drawFittedText("To donate: paypal.me/dsp56300", getLocalBounds(), juce::Justification::centredBottom, 2);
}

void AudioPluginAudioProcessorEditor::resized()
{
	// This is generally where you'll want to lay out the positions of any
	// subcomponents in your editor..
	m_tempEditor.setBounds(0, 35, getWidth(), getHeight() - 35);
}
