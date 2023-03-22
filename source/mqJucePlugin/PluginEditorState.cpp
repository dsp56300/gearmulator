#include "PluginEditorState.h"

#include "mqEditor.h"
#include "PluginProcessor.h"

#include "../synthLib/os.h"

const std::vector<PluginEditorState::Skin> g_includedSkins =
{
	{"Device", "mqFrontPanel.json", ""},
	{"Editor", "mqDefault.json", ""}
};

PluginEditorState::PluginEditorState(AudioPluginAudioProcessor& _processor) : jucePluginEditorLib::PluginEditorState(_processor, _processor.getController(), g_includedSkins)
{
	loadDefaultSkin();
}

genericUI::Editor* PluginEditorState::createEditor(const Skin& _skin, std::function<void()> _openMenuCallback)
{
	return new mqJucePlugin::Editor(m_processor, m_parameterBinding, _skin.folder, _skin.jsonFilename);
}
