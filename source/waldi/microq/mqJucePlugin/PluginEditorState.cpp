#include "PluginEditorState.h"

#include "mqEditor.h"
#include "PluginProcessor.h"

#include "skins.h"

namespace mqJucePlugin
{
	PluginEditorState::PluginEditorState(AudioPluginAudioProcessor& _processor) : jucePluginEditorLib::PluginEditorState(_processor, _processor.getController(), g_includedSkins)
	{
		loadDefaultSkin();
	}

	jucePluginEditorLib::Editor* PluginEditorState::createEditor(const jucePluginEditorLib::Skin& _skin)
	{
		return new mqJucePlugin::Editor(m_processor, _skin);
	}
}