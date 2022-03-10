#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "VirusController.h"
#include "version.h"

#include "ui/VirusEditor.h"
#include "ui2/VirusEditor.h"
//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor &p) :
	AudioProcessorEditor(&p), processorRef(p), m_parameterBinding(p), m_scale("Scale"), m_skin("Skin")
{
    ignoreUnused (processorRef);

	setSize(1377, 800);
	const auto config = processorRef.getController().getConfig();
	auto scale = config->getIntValue("scale", 100);
	int skinId = config->getIntValue("skin", 0);
	//m_virusEditor->setTopLeftPosition(0, 0);
	m_scale.setBounds(0,0,74,24);
	m_scale.addItem("50%", 50);
	m_scale.addItem("75%", 75);
	m_scale.addItem("100%", 100);
	m_scale.addItem("125%", 125);
	m_scale.addItem("150%", 150);
	m_scale.addItem("200%", 200);
	
	m_scale.setSelectedId(scale, juce::dontSendNotification);
	m_scale.setColour(juce::ComboBox::textColourId, juce::Colours::white);
	m_scale.onChange = [this, config]() {
		float value = m_scale.getSelectedIdAsValue().getValue();
		setScaleFactor(value/100.0f);
		config->setValue("scale", (int)value);
		config->saveIfNeeded();
	};
	setScaleFactor(scale/100.0f);

	m_skin.setBounds(m_scale.getBounds().getRight() + 4, 0, 74, 24);
	m_skin.addItem("Modern", 1);
	m_skin.addItem("Classic", 2);
	m_skin.setSelectedId(1, juce::dontSendNotification);
	m_skin.setColour(juce::ComboBox::textColourId, juce::Colours::white);
	m_skin.setSelectedItemIndex(skinId, juce::dontSendNotification);
	addAndMakeVisible(m_scale);
	addAndMakeVisible(m_skin);
	m_skin.onChange = [this, config]() {
		int skinId = m_skin.getSelectedItemIndex();
		config->setValue("skin", skinId);
		config->saveIfNeeded();
		LoadSkin(m_skin.getSelectedItemIndex());
	};

	LoadSkin(skinId);
	//addAndMakeVisible(m_virusEditor);
}

void AudioPluginAudioProcessorEditor::LoadSkin(int index) {
	if (m_virusEditor != nullptr && getIndexOfChildComponent(m_virusEditor) > -1)
	{
		removeChildComponent(m_virusEditor);
		delete m_virusEditor;
	}
	
	if (index == 1)
	{
		auto virusEditor = new Trancy::VirusEditor(m_parameterBinding, processorRef);
		setSize(virusEditor->iSkinSizeWidth, virusEditor->iSkinSizeHeight);
		virusEditor->m_AudioPlugInEditor = (AudioPluginAudioProcessorEditor *)this;
		m_virusEditor = (VirusEditor*)virusEditor;
	}
	else {
		m_virusEditor = new VirusEditor(m_parameterBinding, processorRef);
		setSize(1377, 800);
	}
	m_virusEditor->setTopLeftPosition(0, 0);
	addAndMakeVisible(m_virusEditor);
	m_scale.toFront(0);
	m_skin.toFront(0);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor() {
	delete m_virusEditor;
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& g)
{
}

void AudioPluginAudioProcessorEditor::timerCallback()
{
	// ugly (polling!) way for refreshing presets names as this is temporary ui
}

void AudioPluginAudioProcessorEditor::resized()
{
	// This is generally where you'll want to lay out the positions of any
	// subcomponents in your editor..
	auto area = getLocalBounds();
	area.removeFromTop(35);
}
