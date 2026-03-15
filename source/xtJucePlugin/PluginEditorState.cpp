#include "PluginEditorState.h"

#include "xtEditor.h"
#include "PluginProcessor.h"

#include "skins.h"

namespace xtJucePlugin
{
	PluginEditorState::PluginEditorState(AudioPluginAudioProcessor& _processor) : jucePluginEditorLib::PluginEditorState(_processor, _processor.getController(), g_includedSkins)
	{
		loadDefaultSkin();
	}

	void PluginEditorState::initContextMenu(juceRmlUi::Menu& _menu)
	{
		jucePluginEditorLib::PluginEditorState::initContextMenu(_menu);
	}

	void PluginEditorState::openMenu(const Rml::Event& _event)
	{
//		if (dynamic_cast<Graph*>(_event->eventComponent))
//			return;
		jucePluginEditorLib::PluginEditorState::openMenu(_event);
	}

	jucePluginEditorLib::Editor* PluginEditorState::createEditor(const jucePluginEditorLib::Skin& _skin)
	{
		return new xtJucePlugin::Editor(m_processor, _skin);
	}
}