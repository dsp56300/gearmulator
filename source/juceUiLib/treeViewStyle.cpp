#include "treeViewStyle.h"

#include "uiObject.h"

namespace genericUI
{
	void TreeViewStyle::apply(Editor& _editor, const UiObject& _object)
	{
		UiObjectStyle::apply(_editor, _object);

		m_boldRootItems = _object.getPropertyInt("boldRootItems", 1);
	}

	void TreeViewStyle::apply(juce::TreeView& _target) const
	{
		applyColorIfNotZero(_target, juce::TreeView::backgroundColourId, juce::Colour(m_bgColor));
		applyColorIfNotZero(_target, juce::TreeView::linesColourId, m_linesColor);
		applyColorIfNotZero(_target, juce::TreeView::dragAndDropIndicatorColourId, m_dragAndDropIndicatorColor);
		applyColorIfNotZero(_target, juce::TreeView::selectedItemBackgroundColourId, m_selectedItemBgColor);
//		applyColorIfNotZero(_target, juce::TreeView::oddItemsColourId, juce::Colour(0xff333333));
//		applyColorIfNotZero(_target, juce::TreeView::evenItemsColourId, juce::Colour(0xff555555));
	}
}
