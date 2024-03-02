#include "resizerbar.h"

#include "patchmanager.h"

namespace jucePluginEditorLib::patchManager
{
	ResizerBar::ResizerBar(PatchManager& _pm, juce::StretchableLayoutManager* _layout, const int _itemIndexInLayout)
	: StretchableLayoutResizerBar(_layout, _itemIndexInLayout, true)
	, m_patchManager(_pm)
	{
	}

	void ResizerBar::hasBeenMoved()
	{
		juce::StretchableLayoutResizerBar::hasBeenMoved();
	}

	void ResizerBar::paint(juce::Graphics& g)
	{
//		juce::StretchableLayoutResizerBar::paint(g);
	    if (isMouseOver()|| isMouseButtonDown())
		        g.fillAll (m_patchManager.getResizerBarColor());
	}
}
