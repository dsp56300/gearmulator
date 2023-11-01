#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace jucePluginEditorLib::patchManager
{
	class TreeItem : public juce::TreeViewItem
	{
	public:
		TreeItem();

	private:
		bool mightContainSubItems() override
		{
			return true;
		}

		void paintItem(juce::Graphics& _g, int _width, int _height) override;
	};
}
