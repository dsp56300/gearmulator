#include "patchmanagertree.h"

#include "patchmanagertreeitem.h"

namespace jucePluginEditorLib
{
	PatchManagerTree::PatchManagerTree()
	{
		setColour(backgroundColourId, juce::Colour(0xff999999));
		setColour(linesColourId, juce::Colour(0xffffffff));
		setColour(dragAndDropIndicatorColourId, juce::Colour(0xff00ff00));
		setColour(selectedItemBackgroundColourId, juce::Colour(0xffaaaaaa));
		setColour(oddItemsColourId, juce::Colour(0xff333333));
		setColour(evenItemsColourId, juce::Colour(0xff555555));

		auto *rootItem = new PatchManagerTreeItem();
		setRootItem(rootItem);
		setRootItemVisible(false);

		rootItem->addSubItem(new PatchManagerTreeItem());
		rootItem->addSubItem(new PatchManagerTreeItem());
		rootItem->addSubItem(new PatchManagerTreeItem());
	}

	PatchManagerTree::~PatchManagerTree()
	{
		deleteRootItem();
	}
}
