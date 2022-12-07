#include "PluginEditor.h"

#include "PluginEditorState.h"
#include "PluginProcessor.h"

#include "VirusController.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor &p, PluginEditorState& s) :
	AudioProcessorEditor(&p), processorRef(p), m_state(s)
{
	addMouseListener(this, true);

	m_state.evSkinLoaded = [&](juce::Component* _component)
	{
		setUiRoot(_component);
	};

	m_state.evSetGuiScale = [&](const int _scale)
	{
		if(getNumChildComponents() > 0)
			setGuiScale(getChildComponent(0), _scale);
	};

	m_state.enableBindings();

	setUiRoot(m_state.getUiRoot());
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
	m_state.evSetGuiScale = [&](int){};
	m_state.evSkinLoaded = [&](juce::Component*){};

	m_state.disableBindings();

	setUiRoot(nullptr);
}

void AudioPluginAudioProcessorEditor::setGuiScale(juce::Component* _comp, int percent)
{
	if(!_comp)
		return;

	const auto s = static_cast<float>(percent)/100.0f * m_state.getRootScale();
	_comp->setTransform(juce::AffineTransform::scale(s,s));

	setSize(m_state.getWidth() * s, m_state.getHeight() * s);

	auto* config = processorRef.getController().getConfig();
	config->setValue("scale", percent);
	config->saveIfNeeded();
}

void AudioPluginAudioProcessorEditor::setUiRoot(juce::Component* _component)
{
	removeAllChildren();

	if(!_component)
		return;

	const auto& config = processorRef.getController().getConfig();
    const auto scale = config->getIntValue("scale", 100);

	setGuiScale(_component, scale);
	addAndMakeVisible(_component);
}

void AudioPluginAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
	if(!event.mods.isPopupMenu())
	{
		AudioProcessorEditor::mouseDown(event);
		return;
	}

	// file browsers have their own menu, do not display two menus at once
	if(event.eventComponent && event.eventComponent->findParentComponentOfClass<juce::FileBrowserComponent>())
		return;

	if(dynamic_cast<juce::TextEditor*>(event.eventComponent))
		return;

	m_state.openMenu();
}
