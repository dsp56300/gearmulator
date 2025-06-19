#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib::patchManager
{
	class PatchManagerUiJuce;

	class ResizerBar : public juce::StretchableLayoutResizerBar
	{
	public:
		ResizerBar(PatchManagerUiJuce& _pm, juce::StretchableLayoutManager* _layout, int _itemIndexInLayout);
		void hasBeenMoved() override;
		void paint(juce::Graphics& g) override;

		void mouseDoubleClick(const juce::MouseEvent& _e) override;
	private:
		PatchManagerUiJuce& m_patchManager;
	};
}
