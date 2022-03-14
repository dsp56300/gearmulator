#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "BinaryData.h"
#include "VirusController.h"
#include "genericUI/editor.h"

#include "ui/VirusEditor.h"
#include "ui2/VirusEditor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor &p) :
	AudioProcessorEditor(&p), processorRef(p), m_parameterBinding(p), m_scale("Scale"), m_skin("Skin")
{
    ignoreUnused (processorRef);

	const auto config = processorRef.getController().getConfig();
    const auto scale = config->getIntValue("scale", 100);
    const int skinId = config->getIntValue("skin", 0);
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
		const float value = m_scale.getSelectedIdAsValue().getValue();
		setScaleFactor(value/100.0f);
		config->setValue("scale", static_cast<int>(value));
		config->saveIfNeeded();
	};
	setScaleFactor(scale/100.0f);

	m_skin.setBounds(m_scale.getBounds().getX(), m_scale.getBounds().getY() + m_scale.getBounds().getHeight(), 74, 24);
	m_skin.addItem("Modern", 1);
	m_skin.addItem("Classic", 2);
	m_skin.addItem("Generic", 3);
	m_skin.setSelectedId(1, juce::dontSendNotification);
	m_skin.setColour(juce::ComboBox::textColourId, juce::Colours::white);
	m_skin.setSelectedItemIndex(skinId, juce::dontSendNotification);
	addAndMakeVisible(m_scale);
	addAndMakeVisible(m_skin);
	m_skin.onChange = [this, config]() {
		const int skinId = m_skin.getSelectedItemIndex();
		config->setValue("skin", skinId);
		config->saveIfNeeded();
		LoadSkin(m_skin.getSelectedItemIndex());
	};

	LoadSkin(skinId);
	//addAndMakeVisible(m_virusEditor);
}

void AudioPluginAudioProcessorEditor::LoadSkin(int index) {
	if(m_currentSkinId == index)
		return;

	m_currentSkinId = index;
	if (m_virusEditor)
	{
		if(getIndexOfChildComponent(m_virusEditor.get()) > -1)
			removeChildComponent(m_virusEditor.get());
		m_virusEditor.reset();
	}

	if (index == 1)
	{
		const auto virusEditor = new Trancy::VirusEditor(m_parameterBinding, processorRef);
		setSize(virusEditor->iSkinSizeWidth, virusEditor->iSkinSizeHeight);
		virusEditor->m_AudioPlugInEditor = this;
		m_virusEditor.reset(virusEditor);
	}
	else if(index == 2)
	{
		try
		{
			m_virusEditor.reset(new genericUI::Editor(std::string(BinaryData::VirusC_json), m_parameterBinding, processorRef.getController()));
			setSize(m_virusEditor->getWidth(), m_virusEditor->getHeight());
		}
		catch(const std::runtime_error& _err)
		{
			LOG("ERROR: Failed to create editor: " << _err.what());
			return;
		}
	}
	else {
		m_virusEditor.reset(new VirusEditor(m_parameterBinding, processorRef));
		setSize(1377, 800);
	}
	m_virusEditor->setTopLeftPosition(0, 0);
	addAndMakeVisible(m_virusEditor.get());
	m_scale.toFront(false);
	m_skin.toFront(false);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor() {
	m_virusEditor.reset();
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
