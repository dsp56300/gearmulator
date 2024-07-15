#pragma once

#include "jucePluginEditorLib/pluginEditorState.h"

class VirusProcessor;

class VirusEditorState : public jucePluginEditorLib::PluginEditorState
{
public:
	explicit VirusEditorState(VirusProcessor& _processor, pluginLib::Controller& _controller, const std::vector<VirusEditorState::Skin>& _includedSkins);

	jucePluginEditorLib::Editor* createEditor(const Skin& _skin) override;

	void initContextMenu(juce::PopupMenu& _menu) override;
	bool initAdvancedContextMenu(juce::PopupMenu& _menu, bool _enabled) override;
};
