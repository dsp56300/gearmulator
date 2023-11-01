#include "treeitem.h"

namespace jucePluginEditorLib::patchManager
{
	TreeItem::TreeItem()
	{
	}

	void TreeItem::paintItem(juce::Graphics& _g, int _width, int _height)
	{
		_g.setColour(juce::Colour(0xffffffff));
		_g.drawText("Root", 0, 0, _width, _height, juce::Justification(juce::Justification::centredLeft));
		TreeViewItem::paintItem(_g, _width, _height);
	}
}
