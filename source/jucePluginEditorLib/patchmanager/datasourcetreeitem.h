#pragma once
#include "treeitem.h"
#include "../../jucePluginLib/patchdb/datasource.h"

namespace pluginLib::patchDB
{
	struct DataSource;
}

namespace jucePluginEditorLib::patchManager
{
	class DatasourceTreeItem : public TreeItem
	{
	public:
		DatasourceTreeItem(PatchManager& _pm, const pluginLib::patchDB::DataSourceNodePtr& _ds);

		bool mightContainSubItems() override
		{
			return m_dataSource->type == pluginLib::patchDB::SourceType::Folder;
		}

		bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails) override { return false; }

	private:
		const pluginLib::patchDB::DataSourceNodePtr m_dataSource;
	};
}
