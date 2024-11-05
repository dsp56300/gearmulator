#pragma once

#include "jucePluginEditorLib/pluginEditorState.h"

namespace virus
{
	class VirusProcessor;

	class VirusEditorState : public jucePluginEditorLib::PluginEditorState
	{
	public:
		explicit VirusEditorState(VirusProcessor& _processor, pluginLib::Controller& _controller, const std::vector<jucePluginEditorLib::Skin>& _includedSkins);

		jucePluginEditorLib::Editor* createEditor(const jucePluginEditorLib::Skin& _skin) override;

		void initContextMenu(juce::PopupMenu& _menu) override;
		bool initAdvancedContextMenu(juce::PopupMenu& _menu, bool _enabled) override;
	};
}