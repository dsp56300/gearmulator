#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "VirusController.h"

#include "ui2/VirusEditor.h"
#include "ui3/VirusEditor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor &p) :
	AudioProcessorEditor(&p), processorRef(p), m_parameterBinding(p)
{
	addMouseListener(this, true);

	const auto config = processorRef.getController().getConfig();
    const auto scale = config->getIntValue("scale", 100);
    const int skinId = config->getIntValue("skin", 0);

	loadSkin(skinId);

	setGuiScale(scale);
}

void AudioPluginAudioProcessorEditor::loadSkin(int index)
{
	if(m_currentSkinId == index)
		return;

	m_currentSkinId = index;

	if (m_virusEditor)
	{
		m_parameterBinding.clearBindings();

		if(getIndexOfChildComponent(m_virusEditor.get()) > -1)
			removeChildComponent(m_virusEditor.get());
		m_virusEditor.reset();
	}

	m_rootScale = 1.0f;

	try
	{
		auto* editor = new genericVirusUI::VirusEditor(m_parameterBinding, processorRef.getController(), processorRef);
		m_virusEditor.reset(editor);
		setSize(m_virusEditor->getWidth(), m_virusEditor->getHeight());
		m_rootScale = editor->getScale();
	}
	catch(const std::runtime_error& _err)
	{
		LOG("ERROR: Failed to create editor: " << _err.what());

		auto* errorLabel = new juce::Label();
		errorLabel->setText(juce::String("Failed to load editor\n\n") + _err.what(), juce::dontSendNotification);
		errorLabel->setJustificationType(juce::Justification::centred);
		errorLabel->setSize(400, 300);

		m_virusEditor.reset(errorLabel);
	}

	m_virusEditor->setTopLeftPosition(0, 0);
	addAndMakeVisible(m_virusEditor.get());
}

void AudioPluginAudioProcessorEditor::setGuiScale(int percent)
{
	setScaleFactor(static_cast<float>(percent)/100.0f * m_rootScale);
	auto* config = processorRef.getController().getConfig();
	config->setValue("scale", percent);
	config->saveIfNeeded();
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
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

void AudioPluginAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
	if(!event.mods.isPopupMenu())
	{
		AudioProcessorEditor::mouseDown(event);
		return;
	}

	const auto config = processorRef.getController().getConfig();
    const auto scale = config->getIntValue("scale", 100);
    const int skinId = config->getIntValue("skin", 0);

	juce::PopupMenu menu;

	juce::PopupMenu skinMenu;
	skinMenu.addItem("Modern", true, skinId == 0,[this] {loadSkin(0);});
	skinMenu.addItem("Classic", true, skinId == 1,[this] {loadSkin(1);});

	juce::PopupMenu scaleMenu;
	scaleMenu.addItem("50%", true, scale == 50, [this] { setGuiScale(50); });
	scaleMenu.addItem("75%", true, scale == 75, [this] { setGuiScale(75); });
	scaleMenu.addItem("100%", true, scale == 100, [this] { setGuiScale(100); });
	scaleMenu.addItem("125%", true, scale == 125, [this] { setGuiScale(125); });
	scaleMenu.addItem("150%", true, scale == 150, [this] { setGuiScale(150); });
	scaleMenu.addItem("200%", true, scale == 200, [this] { setGuiScale(200); });
	scaleMenu.addItem("300%", true, scale == 300, [this] { setGuiScale(300); });

	menu.addSubMenu("GUI Skin", skinMenu);
	menu.addSubMenu("GUI Scale", scaleMenu);

	menu.showMenuAsync(juce::PopupMenu::Options());
}
