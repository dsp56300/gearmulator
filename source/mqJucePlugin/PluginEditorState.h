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
private:
	genericUI::Editor* createEditor(const Skin& _skin, std::function<void()> _openMenuCallback) override;
};
