#pragma once

#include <set>

#include "treeitem.h"
#include "types.h"

#include "../../jucePluginLib/patchdb/patchdbtypes.h"

namespace pluginLib::patchDB
{
	struct DataSource;
}

namespace jucePluginEditorLib::patchManager
{
	class DatasourceTreeItem;
	class TagTreeItem;

	class GroupTreeItem : public TreeItem
	{
	public:
		GroupTreeItem(PatchManager& _pm, GroupType _type);

		bool mightContainSubItems() override
		{
			return true;
		}

		void updateFromTags(const std::set<std::string>& _tags);
		void updateFromDataSources(const std::vector<pluginLib::patchDB::DataSourcePtr>& _dataSources);
		void processDirty(const std::set<pluginLib::patchDB::SearchHandle>& _dirtySearches) override;

	private:
		DatasourceTreeItem* createItemForDataSource(const std::shared_ptr<pluginLib::patchDB::DataSource>& _dataSource);

		const GroupType m_type;
		std::map<std::string, TagTreeItem*> m_itemsByTag;
		std::map<pluginLib::patchDB::DataSourcePtr, DatasourceTreeItem*> m_itemsByDataSource;
	};
}
