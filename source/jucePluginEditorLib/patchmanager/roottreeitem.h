#pragma once
#include "treeitem.h"

namespace jucePluginEditorLib::patchManager
{
	class RootTreeItem : public TreeItem
	{
	public:
		explicit RootTreeItem(PatchManager& _pm);

		bool mightContainSubItems() override
		{
			return true;
		}

		bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails) override
		{
			return false;
		}
	};
}
