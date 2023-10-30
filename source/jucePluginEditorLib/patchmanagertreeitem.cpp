#include "patchmanagertreeitem.h"

namespace jucePluginEditorLib
{
	PatchManagerTreeItem::PatchManagerTreeItem()
	{
	}

	void PatchManagerTreeItem::paintItem(juce::Graphics& g, int width, int height)
	{
		g.setColour(juce::Colour(0xffffffff));
		g.drawText("Root", 0, 0, width, height, juce::Justification(juce::Justification::centredLeft));
		TreeViewItem::paintItem(g, width, height);
	}
}
