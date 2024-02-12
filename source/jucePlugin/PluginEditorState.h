#pragma once

#include "../jucePluginEditorLib/pluginEditorState.h"

class AudioPluginAudioProcessor;

class PluginEditorState : public jucePluginEditorLib::PluginEditorState
{
public:
	explicit PluginEditorState(AudioPluginAudioProcessor& _processor, pluginLib::Controller& _controller);

	genericUI::Editor* createEditor(const Skin& _skin, std::function<void()> _openMenuCallback) override;

	void initContextMenu(juce::PopupMenu& _menu) override;
};
