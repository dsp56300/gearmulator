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

		void removeItem(const DatasourceTreeItem* _item);
		void removeDataSource(const pluginLib::patchDB::DataSourceNodePtr& _ds);
		void updateFromDataSources(const std::vector<pluginLib::patchDB::DataSourceNodePtr>& _dataSources);

		void processDirty(const std::set<pluginLib::patchDB::SearchHandle>& _dirtySearches) override;
		void itemClicked(const juce::MouseEvent&) override;

		void setFilter(const std::string& _filter);

	private:
		DatasourceTreeItem* createItemForDataSource(const pluginLib::patchDB::DataSourceNodePtr& _dataSource);
		TagTreeItem* createSubItem(const std::string& _tag);
		bool needsParentItem(const pluginLib::patchDB::DataSourceNodePtr& _ds) const;
		void validateParent(const pluginLib::patchDB::DataSourceNodePtr& _ds, DatasourceTreeItem* _item);
		void validateParent(TagTreeItem* _item);
		bool match(TreeItem& _item) const;

		const GroupType m_type;
		std::map<std::string, TagTreeItem*> m_itemsByTag;
		std::map<pluginLib::patchDB::DataSourceNodePtr, DatasourceTreeItem*> m_itemsByDataSource;
		std::string m_filter;
	};
}
