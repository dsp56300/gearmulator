#include "grouptreeitem.h"

#include "datasourcetreeitem.h"
#include "patchmanager.h"
#include "tagtreeitem.h"
#include "dsp56kEmu/logging.h"

namespace jucePluginEditorLib::patchManager
{
	class DatasourceTreeItem;
	constexpr const char* const g_groupNames[] =
	{
		"Invalid",
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

	void GroupTreeItem::removeItem(const DatasourceTreeItem* _item)
	{
		if (!_item)
			return;

		for(auto it = m_itemsByDataSource.begin(); it != m_itemsByDataSource.end(); ++it)
		{
			if (it->second == _item)
			{
				m_itemsByDataSource.erase(it);
				break;
			}
		}

		while (_item->getNumSubItems())
			removeItem(dynamic_cast<DatasourceTreeItem*>(_item->getSubItem(0)));

		const auto indexInParent = _item->getIndexInParent();

		if (auto* parent = _item->getParentItem())
			parent->removeSubItem(indexInParent, true);
	}

	void GroupTreeItem::removeDataSource(const pluginLib::patchDB::DataSourceNodePtr& _ds)
	{
		const auto it = m_itemsByDataSource.find(_ds);
		if (it == m_itemsByDataSource.end())
			return;
		removeItem(it->second);
	}

	void GroupTreeItem::updateFromDataSources(const std::vector<pluginLib::patchDB::DataSourceNodePtr>& _dataSources)
	{
		const auto previousItems = m_itemsByDataSource;

		for (const auto& previousItem : previousItems)
		{
			const auto& ds = previousItem.first;

			if (std::find(_dataSources.begin(), _dataSources.end(), ds) == _dataSources.end())
				removeDataSource(ds);
		}

		for (const auto& d : _dataSources)
		{
			const auto itExisting = m_itemsByDataSource.find(d);
			if (m_itemsByDataSource.find(d) != m_itemsByDataSource.end())
			{
				validateParent(itExisting->first, itExisting->second);
				continue;
			}

			auto* item = createItemForDataSource(d);

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

			const auto tagType = toTagType(m_type);

			if(m_type == GroupType::DataSources)
			{
				menu.addItem("Add Folder...", [this]
				{
					juce::FileChooser fc("Select Folder");

					if(fc.browseForDirectory())
					{
						const auto result = fc.getResult().getFullPathName().toStdString();
						pluginLib::patchDB::DataSource ds;
						ds.type = pluginLib::patchDB::SourceType::Folder;
						ds.name = result;
						ds.origin = pluginLib::patchDB::DataSourceOrigin::Manual;
						getPatchManager().addDataSource(ds);
					}
				});

				menu.addItem("Add File...", [this]
				{
					juce::FileChooser fc("Select File");
					if(fc.browseForFileToOpen())
					{
						const auto result = fc.getResult().getFullPathName().toStdString();
						pluginLib::patchDB::DataSource ds;
						ds.type = pluginLib::patchDB::SourceType::File;
						ds.name = result;
						ds.origin = pluginLib::patchDB::DataSourceOrigin::Manual;
						getPatchManager().addDataSource(ds);
					}
				});
			}
			if(tagType != pluginLib::patchDB::TagType::Invalid)
			{
				menu.addItem("Add...", [this]
				{
					beginEdit("Enter name...", [this](bool _success, const std::string& _newText)
					{
						if (!_newText.empty())
							getPatchManager().addTag(toTagType(m_type), _newText);
					});
				});
			}

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

		if(needsParentItem(_dataSource))
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
		auto item = new TagTreeItem(getPatchManager(), m_type, _tag);

		addSubItem(item);
		m_itemsByTag.insert({ _tag, item });

		return item;
	}

	bool GroupTreeItem::needsParentItem(const pluginLib::patchDB::DataSourceNodePtr& _ds)
	{
		return _ds->parent && _ds->origin != pluginLib::patchDB::DataSourceOrigin::Manual;
	}

	void GroupTreeItem::validateParent(const pluginLib::patchDB::DataSourceNodePtr& _ds, DatasourceTreeItem* _item)
	{
		TreeViewItem* parentNeeded = this;

		if(needsParentItem(_ds))
		{
			parentNeeded = createItemForDataSource(_ds->parent);
		}

		auto* parentExisting = _item->getParentItem();

		if(parentNeeded != parentExisting)
		{
			parentExisting->removeSubItem(_item->getIndexInParent(), false);
			parentNeeded->addSubItem(_item);
		}
	}
}
