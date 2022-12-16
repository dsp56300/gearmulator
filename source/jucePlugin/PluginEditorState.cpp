#include "PluginEditorState.h"

#include "PluginProcessor.h"

#include "ui3/VirusEditor.h"

#include "../synthLib/os.h"

const std::vector<PluginEditorState::Skin> g_includedSkins =
{
	{"TI Trancy", "VirusTI_Trancy.json", ""}
};

PluginEditorState::PluginEditorState(AudioPluginAudioProcessor& _processor, pluginLib::Controller& _controller) : jucePluginEditorLib::PluginEditorState(_processor, _controller, g_includedSkins)
{
	loadDefaultSkin();
}

genericUI::Editor* PluginEditorState::createEditor(const Skin& _skin, std::function<void()> _openMenuCallback)
{
	return new genericVirusUI::VirusEditor(m_parameterBinding, static_cast<AudioPluginAudioProcessor&>(m_processor), _skin.jsonFilename, _skin.folder, _openMenuCallback);
}
