#pragma once
#include "treeitem.h"

namespace jucePluginEditorLib::patchManager
{
	class RootTreeItem : public TreeItem
	{
	public:
		explicit RootTreeItem(PatchManagerUiJuce& _pm);

		bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails) override
		{
			return false;
		}
	};
}
