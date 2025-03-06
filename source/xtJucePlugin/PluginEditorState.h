#pragma once

#include "jucePluginEditorLib/pluginEditorState.h"

namespace juce
{
	class Component;
}

namespace xtJucePlugin
{
	class AudioPluginAudioProcessor;

	class PluginEditorState : public jucePluginEditorLib::PluginEditorState
	{
	public:
		explicit PluginEditorState(AudioPluginAudioProcessor& _processor);
		void initContextMenu(juce::PopupMenu& _menu) override;
		bool initAdvancedContextMenu(juce::PopupMenu& _menu, bool _enabled) override;
		void openMenu(const juce::MouseEvent* _event) override;

	private:
		jucePluginEditorLib::Editor* createEditor(const jucePluginEditorLib::Skin& _skin) override;
	};
}
