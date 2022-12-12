#include "PluginEditorState.h"

#include "PluginProcessor.h"

#include "../synthLib/os.h"

PluginEditorState::PluginEditorState(AudioPluginAudioProcessor& _processor) : jucePluginEditorLib::PluginEditorState(_processor, _processor.getController(), {})
{
}

genericUI::Editor* PluginEditorState::createEditor(const Skin& _skin, std::function<void()> _openMenuCallback)
{
	return nullptr;
}
