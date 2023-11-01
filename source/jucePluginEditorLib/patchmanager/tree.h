#pragma once

#include <juce_audio_plugin_client/juce_audio_plugin_client.h>

namespace jucePluginEditorLib::patchManager
{
	class Tree : public juce::TreeView
	{
	public:
		Tree();
		~Tree() override;

	private:
	};
}
