#pragma once
#include "treeitem.h"

namespace pluginLib::patchDB
{
	struct DataSource;
}

namespace jucePluginEditorLib::patchManager
{
	class DatasourceTreeItem : public TreeItem
	{
	public:
		DatasourceTreeItem(PatchManager& _pm, const pluginLib::patchDB::DataSource& _ds);

		bool mightContainSubItems() override
		{
			return false;
		}

		void processSearchUpdated(const pluginLib::patchDB::Search& _search) override;

		bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override
		{
				return false;
		}
	};
}
