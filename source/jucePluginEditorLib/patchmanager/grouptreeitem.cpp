#include "grouptreeitem.h"

#include "datasourcetreeitem.h"
#include "patchmanager.h"
#include "search.h"
#include "tagtreeitem.h"
#include "dsp56kEmu/logging.h"

namespace jucePluginEditorLib::patchManager
{
	class DatasourceTreeItem;
	constexpr const char* const g_groupNames[] =
	{
		"Invalid",
		"Data Sources",
		"User",
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
		for(auto it = m_itemsByTag.begin(); it != m_itemsByTag.end();)
		{
			const auto tag = it->first;
			const auto* item = it->second;

			if(_tags.find(tag) == _tags.end())
			{
				item->removeFromParent(true);
				m_itemsByTag.erase(it++);
			}
			else
			{
				++it;
			}
		}

		for (const auto& tag : _tags)
		{
			auto itExisting = m_itemsByTag.find(tag);

			if (itExisting != m_itemsByTag.end())
			{
				validateParent(itExisting->second);
				continue;
			}

			const auto oldNumSubItems = getNumSubItems();
			createSubItem(tag);

			if (getNumSubItems() == 1 && oldNumSubItems == 0)
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

		_item->removeFromParent(true);
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
				itExisting->second->refresh();
				continue;
			}

			auto* item = createItemForDataSource(d);

			m_itemsByDataSource.insert({ d, item });
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
			else if(m_type ==GroupType::LocalStorage)
			{
				menu.addItem("Create...", [this]
				{
					beginEdit("Enter name...", [this](bool _success, const std::string& _newText)
					{
						pluginLib::patchDB::DataSource ds;

						ds.name = _newText;
						ds.type = pluginLib::patchDB::SourceType::LocalStorage;
						ds.origin = pluginLib::patchDB::DataSourceOrigin::Manual;
						ds.timestamp = std::chrono::system_clock::now();

						getPatchManager().addDataSource(ds);
					});
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

	void GroupTreeItem::setFilter(const std::string& _filter)
	{
		if (m_filter == _filter)
			return;

		m_filter = _filter;

		for (const auto& it : m_itemsByDataSource)
			validateParent(it.first, it.second);

		for (const auto& it : m_itemsByTag)
			validateParent(it.second);
	}

	bool GroupTreeItem::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails)
	{
		if (isOpen())
			return false;

		if( (!m_itemsByDataSource.empty() && m_itemsByDataSource.begin()->second->isInterestedInDragSource(_dragSourceDetails)) || 
			(!m_itemsByTag.empty() && m_itemsByTag.begin()->second->isInterestedInDragSource(_dragSourceDetails)))
			setOpen(true);

		return false;
	}

	DatasourceTreeItem* GroupTreeItem::selectItem(const pluginLib::patchDB::DataSource& _ds) const
	{
		for (const auto& [_, item] : m_itemsByDataSource)
		{
			if(*item->getDataSource() == _ds)
			{
				item->setSelected(true, true);
				return item;
			}
		}
		return nullptr;
	}

	DatasourceTreeItem* GroupTreeItem::createItemForDataSource(const pluginLib::patchDB::DataSourceNodePtr& _dataSource)
	{
		const auto it = m_itemsByDataSource.find(_dataSource);

		if (it != m_itemsByDataSource.end())
			return it->second;

		auto* item = new DatasourceTreeItem(getPatchManager(), _dataSource);

		m_itemsByDataSource.insert({ _dataSource, item });

		const auto oldNumSubItems = getNumSubItems();

		validateParent(_dataSource, item);

		if(getNumSubItems() == 1 && oldNumSubItems == 0)
			setOpen(true);

		return item;
	}

	TagTreeItem* GroupTreeItem::createSubItem(const std::string& _tag)
	{
		auto item = new TagTreeItem(getPatchManager(), m_type, _tag);

		validateParent(item);

		m_itemsByTag.insert({ _tag, item });

		return item;
	}

	bool GroupTreeItem::needsParentItem(const pluginLib::patchDB::DataSourceNodePtr& _ds) const
	{
		if (!m_filter.empty())
			return false;
		return _ds->hasParent() && _ds->origin != pluginLib::patchDB::DataSourceOrigin::Manual;
	}

	void GroupTreeItem::validateParent(const pluginLib::patchDB::DataSourceNodePtr& _ds, DatasourceTreeItem* _item)
	{
		TreeViewItem* parentNeeded = nullptr;

		if (needsParentItem(_ds))
		{
			parentNeeded = createItemForDataSource(_ds->getParent());
		}
		else if (_ds->type == pluginLib::patchDB::SourceType::Folder && !m_filter.empty())
		{
			parentNeeded = nullptr;
		}
		else if (match(*_item))
		{
			parentNeeded = this;
		}

		_item->setParent(parentNeeded, true);
	}

	void GroupTreeItem::validateParent(TagTreeItem* _item)
	{
		if (match(*_item))
			_item->setParent(this, true);
		else
			_item->setParent(nullptr, true);
	}

	bool GroupTreeItem::match(const TreeItem& _item) const
	{
		if (m_filter.empty())
			return true;
		const auto t = Search::lowercase(_item.getText());
		return t.find(m_filter) != std::string::npos;
	}
}
