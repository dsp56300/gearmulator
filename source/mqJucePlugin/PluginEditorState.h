#pragma once

#include <functional>

#include "../jucePluginEditorLib/pluginEditorState.h"

namespace juce
{
	class Component;
}

class AudioPluginAudioProcessor;

class PluginEditorState : public jucePluginEditorLib::PluginEditorState
{
public:
	explicit PluginEditorState(AudioPluginAudioProcessor& _processor);
	void initContextMenu(juce::PopupMenu& _menu) override;
private:
	genericUI::Editor* createEditor(const Skin& _skin, std::function<void()> _openMenuCallback) override;
};
