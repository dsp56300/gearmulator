#pragma once

#include "jucePluginEditorLib/pluginEditorState.h"

namespace juce
{
	class Component;
}

namespace n2xJucePlugin
{
	class AudioPluginAudioProcessor;

	class PluginEditorState : public jucePluginEditorLib::PluginEditorState
	{
	public:
		explicit PluginEditorState(AudioPluginAudioProcessor& _processor);

	private:
		jucePluginEditorLib::Editor* createEditor(const jucePluginEditorLib::Skin& _skin) override;
	};
}
