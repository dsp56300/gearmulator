#include "tree.h"

#include "treeitem.h"

namespace jucePluginEditorLib::patchManager
{
	Tree::Tree()
	{
		setColour(backgroundColourId, juce::Colour(0xff999999));
		setColour(linesColourId, juce::Colour(0xffffffff));
		setColour(dragAndDropIndicatorColourId, juce::Colour(0xff00ff00));
		setColour(selectedItemBackgroundColourId, juce::Colour(0xffaaaaaa));
		setColour(oddItemsColourId, juce::Colour(0xff333333));
		setColour(evenItemsColourId, juce::Colour(0xff555555));

		auto *rootItem = new TreeItem();
		setRootItem(rootItem);
		setRootItemVisible(false);

		rootItem->addSubItem(new TreeItem());
		rootItem->addSubItem(new TreeItem());
		rootItem->addSubItem(new TreeItem());
	}

	Tree::~Tree()
	{
		deleteRootItem();
	}
}
