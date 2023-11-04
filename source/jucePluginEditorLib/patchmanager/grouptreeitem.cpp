#include "grouptreeitem.h"

#include "categorytreeitem.h"
#include "datasourcetreeitem.h"
#include "patchmanager.h"
#include "tagtreeitem.h"
#include "dsp56kEmu/logging.h"

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

			if (itExisting != m_itemsByTag.end())
				continue;

			createSubItem(tag);

			if (m_itemsByTag.size() == 1)
				setOpen(true);
		}
	}

	void GroupTreeItem::updateFromDataSources(const std::vector<pluginLib::patchDB::DataSourceNodePtr>& _dataSources)
	{
		for (const auto& d : _dataSources)
		{
			createItemForDataSource(d);

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

	void GroupTreeItem::itemClicked(const juce::MouseEvent& _mouseEvent)
	{
		if(_mouseEvent.mods.isPopupMenu())
		{
			TreeItem::itemClicked(_mouseEvent);

			juce::PopupMenu menu;

			menu.addItem("Add...", [this]
			{
				beginEdit("Enter name...", [this](bool _success, const std::string& _newText)
				{
					LOG("New Text: " << _newText << ", success " << _success);
					getPatchManager().addTag(pluginLib::patchDB::TagType::Tag, _newText);
				});
			});

			menu.showMenuAsync(juce::PopupMenu::Options());
		}
	}

	DatasourceTreeItem* GroupTreeItem::createItemForDataSource(const pluginLib::patchDB::DataSourceNodePtr& _dataSource)
	{
		const auto it = m_itemsByDataSource.find(_dataSource);

		if (it != m_itemsByDataSource.end())
			return it->second;

		auto* item = new DatasourceTreeItem(getPatchManager(), _dataSource);

		m_itemsByDataSource.insert({ _dataSource, item });

		if(_dataSource->parent)
		{
			auto* parent = createItemForDataSource(_dataSource->parent);
			parent->addSubItem(item);
		}
		else
		{
			addSubItem(item);

			if (getNumSubItems() == 1)
				setOpen(true);
		}

		return item;
	}

	TagTreeItem* GroupTreeItem::createSubItem(const std::string& _tag)
	{
		TagTreeItem* item = m_type == GroupType::Categories ? new CategoryTreeItem(getPatchManager(), _tag) : new TagTreeItem(getPatchManager(), GroupType::Tags, _tag);

		addSubItem(item);
		m_itemsByTag.insert({ _tag, item });

		return item;
	}
}
