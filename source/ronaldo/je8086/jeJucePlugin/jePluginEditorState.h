#pragma once

#include "jucePluginEditorLib/pluginEditorState.h"

namespace juce
{
	class Component;
}

namespace jeJucePlugin
{
	class AudioPluginAudioProcessor;

	class PluginEditorState : public jucePluginEditorLib::PluginEditorState
	{
	public:
		explicit PluginEditorState(AudioPluginAudioProcessor& _processor);
		void initContextMenu(juceRmlUi::Menu& _menu) override;

	private:
		jucePluginEditorLib::Editor* createEditor(const jucePluginEditorLib::Skin& _skin) override;
	};
}
