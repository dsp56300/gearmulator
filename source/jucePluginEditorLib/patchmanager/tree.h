#pragma once

#include <juce_audio_plugin_client/juce_audio_plugin_client.h>

namespace jucePluginEditorLib
{
	class PatchManagerTree : public juce::TreeView
	{
	public:
		PatchManagerTree();
		~PatchManagerTree() override;

	private:
	};
}
