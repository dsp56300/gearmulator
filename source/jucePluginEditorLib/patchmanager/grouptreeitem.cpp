#include "grouptreeitem.h"

#include "categorytreeitem.h"
#include "datasourcetreeitem.h"
#include "patchmanager.h"
#include "tagtreeitem.h"

namespace jucePluginEditorLib::patchManager
{
	class DatasourceTreeItem;
	constexpr const char* const g_groupNames[] =
	{
		"Data Sources",
		"Categories",
		"Tags",
		"Favourites"
	};

	static_assert(std::size(g_groupNames) == static_cast<uint32_t>(GroupType::Count));

	GroupTreeItem::GroupTreeItem(PatchManager& _pm, const GroupType _type) : TreeItem(_pm, g_groupNames[static_cast<uint32_t>(_type)]), m_type(_type)
	{
	}

	void GroupTreeItem::updateFromTags(const std::set<std::string>& _tags)
	{
		for (const auto& tag : _tags)
		{
			auto itExisting = m_itemsByTag.find(tag);

			if(itExisting != m_itemsByTag.end())
				continue;

			TagTreeItem* item = m_type == GroupType::Categories ? new CategoryTreeItem(getPatchManager(), tag) : new TagTreeItem(getPatchManager(), GroupType::Tags, tag);

			addSubItem(item);
			m_itemsByTag.insert({ tag, item });

			if (m_itemsByTag.size() == 1)
				setOpen(true);
		}
	}

	void GroupTreeItem::updateFromDataSources(const std::vector<pluginLib::patchDB::DataSource>& _dataSources)
	{
		for (const auto& d : _dataSources)
		{
			if (m_itemsByDataSource.find(d) != m_itemsByDataSource.end())
				continue;
			auto* item = new DatasourceTreeItem(getPatchManager(), d);

			addSubItem(item);
			m_itemsByDataSource.insert({ d, item });

			if (m_itemsByDataSource.size() == 1)
				setOpen(true);
		}
	}

	void GroupTreeItem::processDirty(const std::set<pluginLib::patchDB::SearchHandle>& _dirtySearches)
	{
		for (const auto& it : m_itemsByTag)
			it.second->processDirty(_dirtySearches);

		for (const auto& it : m_itemsByDataSource)
			it.second->processDirty(_dirtySearches);
	}
}
