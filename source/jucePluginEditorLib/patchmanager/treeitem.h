#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace jucePluginEditorLib
{
	class PatchManagerTreeItem : public juce::TreeViewItem
	{
	public:
		PatchManagerTreeItem();

	private:
		bool mightContainSubItems() override
		{
			return true;
		}

		void paintItem(juce::Graphics& g, int width, int height) override;
	};
}
