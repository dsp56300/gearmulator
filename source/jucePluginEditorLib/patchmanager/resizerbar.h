#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib::patchManager
{
	class PatchManager;

	class ResizerBar : public juce::StretchableLayoutResizerBar
	{
	public:
		ResizerBar(PatchManager& _pm, juce::StretchableLayoutManager* _layout, int _itemIndexInLayout);
		void hasBeenMoved() override;
		void paint(juce::Graphics& g) override;

	private:
		PatchManager& m_patchManager;
	};
}
