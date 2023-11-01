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
		void updateFromDataSources(const std::vector<pluginLib::patchDB::DataSource>& _dataSources);
		void processDirty(const std::set<pluginLib::patchDB::SearchHandle>& _dirtySearches) override;

	private:
		const GroupType m_type;
		std::map<std::string, TagTreeItem*> m_itemsByTag;
		std::map<pluginLib::patchDB::DataSource, TreeItem*> m_itemsByDataSource;
	};
}
