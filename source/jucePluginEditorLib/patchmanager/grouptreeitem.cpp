#include "grouptreeitem.h"

#include "datasourcetreeitem.h"
#include "patchmanager.h"
#include "search.h"
#include "tagtreeitem.h"

#include "synthLib/os.h"

namespace jucePluginEditorLib::patchManager
{
	class DatasourceTreeItem;

	GroupTreeItem::GroupTreeItem(PatchManager& _pm, const GroupType _type, const std::string& _groupName) : TreeItem(_pm, _groupName), m_type(_type)
	{
		GroupTreeItem::onParentSearchChanged({});
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

		setDeselectonSecondClick(true);
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

		TreeItem::processDirty(_dirtySearches);
	}

	void GroupTreeItem::itemClicked(const juce::MouseEvent& _mouseEvent)
	{
		if(!_mouseEvent.mods.isPopupMenu())
		{
			TreeItem::itemClicked(_mouseEvent);
			return;
		}

		juce::PopupMenu menu;

		const auto tagType = toTagType(m_type);

		if(m_type == GroupType::DataSources)
		{
			menu.addItem("Add folders...", [this]
			{
				juce::FileChooser fc("Select Folders");

				if(fc.showDialog(
					juce::FileBrowserComponent::openMode | 
					juce::FileBrowserComponent::canSelectDirectories | 
					juce::FileBrowserComponent::canSelectMultipleItems
					, nullptr))
				{
					for (const auto& r : fc.getResults())
					{
						const auto result = r.getFullPathName().toStdString();
						pluginLib::patchDB::DataSource ds;
						ds.type = pluginLib::patchDB::SourceType::Folder;
						ds.name = result;
						ds.origin = pluginLib::patchDB::DataSourceOrigin::Manual;
						getPatchManager().addDataSource(ds);
					}
				}
			});

			menu.addItem("Add files...", [this]
			{
				juce::FileChooser fc("Select Files");
				if(fc.showDialog(
					juce::FileBrowserComponent::openMode |
					juce::FileBrowserComponent::canSelectFiles |
					juce::FileBrowserComponent::canSelectMultipleItems,
					nullptr))
				{
					for (const auto&r : fc.getResults())
					{
						const auto result = r.getFullPathName().toStdString();
						pluginLib::patchDB::DataSource ds;
						ds.type = pluginLib::patchDB::SourceType::File;
						ds.name = result;
						ds.origin = pluginLib::patchDB::DataSourceOrigin::Manual;
						getPatchManager().addDataSource(ds);
					}
				}
			});
		}
		else if(m_type == GroupType::LocalStorage)
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
		// if there are no favourites yet, allow to drag onto this group node and create a default tag automatically if patches are dropped
		if(m_type == GroupType::Favourites && m_itemsByTag.empty() && TreeItem::isInterestedInDragSource(_dragSourceDetails))
			return true;

		if (isOpen())
			return false;

		if( (!m_itemsByDataSource.empty() && m_itemsByDataSource.begin()->second->isInterestedInDragSource(_dragSourceDetails)) || 
			(!m_itemsByTag.empty() && m_itemsByTag.begin()->second->isInterestedInDragSource(_dragSourceDetails)))
			setOpen(true);

		return false;
	}

	DatasourceTreeItem* GroupTreeItem::getItem(const pluginLib::patchDB::DataSource& _ds) const
	{
		for (const auto& [_, item] : m_itemsByDataSource)
		{
			if(*item->getDataSource() == _ds)
				return item;
		}
		return nullptr;
	}

	void GroupTreeItem::setParentSearchRequest(const pluginLib::patchDB::SearchRequest& _parentSearch)
	{
		TreeItem::setParentSearchRequest(_parentSearch);

		for (const auto& it : m_itemsByDataSource)
			it.second->setParentSearchRequest(_parentSearch);

		for (const auto& it : m_itemsByTag)
			it.second->setParentSearchRequest(_parentSearch);
	}

	void GroupTreeItem::onParentSearchChanged(const pluginLib::patchDB::SearchRequest& _parentSearchRequest)
	{
		TreeItem::onParentSearchChanged(_parentSearchRequest);

		const auto sourceType = toSourceType(m_type);

		if(sourceType != pluginLib::patchDB::SourceType::Invalid)
		{
			pluginLib::patchDB::SearchRequest req = _parentSearchRequest;
			req.sourceType = sourceType;
			search(std::move(req));
		}

		const auto tagType = toTagType(m_type);

		if(tagType != pluginLib::patchDB::TagType::Invalid)
		{
			pluginLib::patchDB::SearchRequest req = _parentSearchRequest;
			req.anyTagOfType.insert(tagType);
			search(std::move(req));
		}
	}

	bool GroupTreeItem::isInterestedInPatchList(const ListModel* _list, const std::vector<pluginLib::patchDB::PatchPtr>& _patches)
	{
		if(m_type == GroupType::Favourites)
			return true;
		return TreeItem::isInterestedInPatchList(_list, _patches);
	}

	void GroupTreeItem::patchesDropped(const std::vector<pluginLib::patchDB::PatchPtr>& _patches, const SavePatchDesc* _savePatchDesc)
	{
		if(!m_itemsByTag.empty() || m_type != GroupType::Favourites)
		{
			TreeItem::patchesDropped(_patches, _savePatchDesc);
			return;
		}

		const auto tagType = toTagType(m_type);

		if(tagType == pluginLib::patchDB::TagType::Invalid)
		{
			TreeItem::patchesDropped(_patches, _savePatchDesc);
			return;
		}

		constexpr const char* const tag = "Favourites";

		getPatchManager().addTag(tagType, tag);
		TagTreeItem::modifyTags(getPatchManager(), tagType, tag, _patches);
	}

	bool GroupTreeItem::isInterestedInFileDrag(const juce::StringArray& _files)
	{
		if(_files.isEmpty())
			return TreeItem::isInterestedInFileDrag(_files);

		switch (m_type)
		{
		case GroupType::DataSources:
			{
				// do not allow to add data sources from temporary directory
				const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory).getFullPathName().toStdString();
				for (const auto& file : _files)
				{
					if(file.toStdString().find(tempDir) == 0)
						return false;
				}
				return true;
			}
		case GroupType::LocalStorage:
			return true;
		default:
			return TreeItem::isInterestedInFileDrag(_files);
		}
	}

	void GroupTreeItem::filesDropped(const juce::StringArray& _files, const int _insertIndex)
	{
		if(m_type == GroupType::DataSources)
		{
			for (const auto& file : _files)
			{
				pluginLib::patchDB::DataSource ds;
				ds.name = file.toStdString();
				ds.type = pluginLib::patchDB::SourceType::File;
				ds.origin = pluginLib::patchDB::DataSourceOrigin::Manual;

				getPatchManager().addDataSource(ds);
			}
		}
		else if(m_type == GroupType::LocalStorage)
		{
			for (const auto& file : _files)
			{
				auto patches = getPatchManager().loadPatchesFromFiles(std::vector<std::string>{file.toStdString()});

				if(patches.empty())
					continue;

				pluginLib::patchDB::DataSource ds;
				ds.name = synthLib::getFilenameWithoutPath(file.toStdString());
				ds.type = pluginLib::patchDB::SourceType::LocalStorage;
				ds.origin = pluginLib::patchDB::DataSourceOrigin::Manual;

				getPatchManager().addDataSource(ds, [this, patches](const bool _success, const std::shared_ptr<pluginLib::patchDB::DataSourceNode>& _ds)
				{
					if(_success)
						getPatchManager().copyPatchesToLocalStorage(_ds, patches, -1);
				});
			}
		}
		else
		{
			TreeItem::filesDropped(_files, _insertIndex);
		}
	}

	DatasourceTreeItem* GroupTreeItem::createItemForDataSource(const pluginLib::patchDB::DataSourceNodePtr& _dataSource)
	{
		const auto it = m_itemsByDataSource.find(_dataSource);

		if (it != m_itemsByDataSource.end())
			return it->second;

		auto* item = new DatasourceTreeItem(getPatchManager(), _dataSource);

		m_itemsByDataSource.insert({ _dataSource, item });

		validateParent(_dataSource, item);

		return item;
	}

	TagTreeItem* GroupTreeItem::createSubItem(const std::string& _tag)
	{
		auto item = new TagTreeItem(getPatchManager(), m_type, _tag);

		validateParent(item);

		m_itemsByTag.insert({ _tag, item });

		item->onParentSearchChanged(getParentSearchRequest());

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
