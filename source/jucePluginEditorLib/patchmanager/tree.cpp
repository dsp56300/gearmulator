#include "tree.h"

#include <set>

#include "grouptreeitem.h"
#include "patchmanager.h"
#include "roottreeitem.h"
#include "treeitem.h"
#include "defaultskin.h"

#include "../../juceUiLib/uiObject.h"
#include "../pluginEditor.h"

namespace jucePluginEditorLib::patchManager
{
	constexpr const char* const g_groupNames[] =
	{
		"Invalid",
		"Data Sources",
		"User",
		"Categories",
		"Tags",
		"Favourites",
		"CustomA",
		"CustomB",
		"CustomC"
	};

	static_assert(std::size(g_groupNames) == static_cast<uint32_t>(GroupType::Count));

	Tree::Tree(PatchManager& _patchManager) : m_patchManager(_patchManager)
	{
		// some very basic defaults if no style is available
		setColour(backgroundColourId, juce::Colour(defaultSkin::colors::background));
//		setColour(backgroundColourId, juce::Colour(0));
		setColour(linesColourId, juce::Colour(0xffffffff));
		setColour(dragAndDropIndicatorColourId, juce::Colour(0xff00ff00));
		setColour(selectedItemBackgroundColourId, juce::Colour(defaultSkin::colors::selectedItem));
//		setColour(oddItemsColourId, juce::Colour(0xff333333));
//		setColour(evenItemsColourId, juce::Colour(0xff555555));

		if(const auto t = _patchManager.getTemplate("pm_treeview"))
		{
			t->apply(_patchManager.getEditor(), *this);
		}
		
		auto *rootItem = new RootTreeItem(m_patchManager);
		setRootItem(rootItem);
		setRootItemVisible(false);

		getViewport()->setScrollBarsShown(true, true);

		if(const auto t = _patchManager.getTemplate("pm_scrollbar"))
		{
			t->apply(_patchManager.getEditor(), getViewport()->getVerticalScrollBar());
			t->apply(_patchManager.getEditor(), getViewport()->getHorizontalScrollBar());
		}
		else
		{
			getViewport()->getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId, juce::Colour(defaultSkin::colors::scrollbar));
			getViewport()->getVerticalScrollBar().setColour(juce::ScrollBar::trackColourId, juce::Colour(defaultSkin::colors::scrollbar));
			getViewport()->getHorizontalScrollBar().setColour(juce::ScrollBar::thumbColourId, juce::Colour(defaultSkin::colors::scrollbar));
			getViewport()->getHorizontalScrollBar().setColour(juce::ScrollBar::trackColourId, juce::Colour(defaultSkin::colors::scrollbar));
		}
	}

	Tree::~Tree()
	{
		deleteRootItem();
	}

	void Tree::updateDataSources()
	{
		auto* itemDs = getItem(GroupType::DataSources);
		auto* itemLocalStorage = getItem(GroupType::LocalStorage);

		if (!itemDs || !itemLocalStorage)
			return;

		std::vector<pluginLib::patchDB::DataSourceNodePtr> allDataSources;

		std::vector<pluginLib::patchDB::DataSourceNodePtr> readOnlyDataSources;
		std::vector<pluginLib::patchDB::DataSourceNodePtr> storageDataSources;

		m_patchManager.getDataSources(allDataSources);

		readOnlyDataSources.reserve(allDataSources.size());
		storageDataSources.reserve(allDataSources.size());

		for (const auto& ds : allDataSources)
		{
			if (ds->type == pluginLib::patchDB::SourceType::LocalStorage)
				storageDataSources.push_back(ds);
			else
				readOnlyDataSources.push_back(ds);
		}

		itemDs->updateFromDataSources(readOnlyDataSources);
		itemLocalStorage->updateFromDataSources(storageDataSources);
	}

	void Tree::updateTags(const GroupType _type)
	{
		const auto tagType = toTagType(_type);
		if (tagType == pluginLib::patchDB::TagType::Invalid)
			return;
		auto* item = getItem(_type);
		if (!item)
			return;
		std::set<pluginLib::patchDB::Tag> tags;
		m_patchManager.getTags(tagType, tags);
		item->updateFromTags(tags);
	}

	void Tree::updateTags(const pluginLib::patchDB::TagType _type)
	{
		const auto groupType = toGroupType(_type);
		if (groupType == GroupType::Invalid)
			return;
		updateTags(groupType);
	}

	void Tree::processDirty(const pluginLib::patchDB::Dirty& _dirty)
	{
		if (_dirty.dataSources)
			updateDataSources();

		for (const auto& tagType : _dirty.tags)
			updateTags(tagType);

		if (!_dirty.searches.empty())
		{
			for (const auto& it : m_groupItems)
				it.second->processDirty(_dirty.searches);
		}
	}

	void Tree::paint(juce::Graphics& g)
	{
		if (findColour(backgroundColourId).getAlpha() > 0)
			TreeView::paint(g);
	}

	bool Tree::keyPressed(const juce::KeyPress& _key)
	{
		if(_key.getKeyCode() == juce::KeyPress::F2Key)
		{
			if(getNumSelectedItems() == 1)
			{
				juce::TreeViewItem* item = getSelectedItem(0);
				auto* myItem = dynamic_cast<TreeItem*>(item);

				if(myItem)
					return myItem->beginEdit();
			}
		}
		return TreeView::keyPressed(_key);
	}

	void Tree::setFilter(const std::string& _filter)
	{
		if (m_filter == _filter)
			return;

		m_filter = _filter;

		for (const auto& it : m_groupItems)
			it.second->setFilter(_filter);
	}

	DatasourceTreeItem* Tree::getItem(const pluginLib::patchDB::DataSource& _ds)
	{
		const auto it = m_groupItems.find(_ds.type == pluginLib::patchDB::SourceType::LocalStorage ?  GroupType::LocalStorage : GroupType::DataSources);
		if(it == m_groupItems.end())
			return nullptr;
		const auto* item = it->second;
		return item->getItem(_ds);
	}

	void Tree::onParentSearchChanged(const pluginLib::patchDB::SearchRequest& _searchRequest)
	{
		for (const auto& groupItem : m_groupItems)
		{
			groupItem.second->setParentSearchRequest(_searchRequest);
		}
	}

	void Tree::addGroup(GroupType _type, const std::string& _name)
	{
		auto* groupItem = new GroupTreeItem(m_patchManager, _type, _name);
		getRootItem()->addSubItem(groupItem);
		m_groupItems.insert({ _type, groupItem });
		groupItem->setFilter(m_filter);
	}

	void Tree::addGroup(const GroupType _type)
	{
		addGroup(_type, g_groupNames[static_cast<uint32_t>(_type)]);
	}

	GroupTreeItem* Tree::getItem(const GroupType _type)
	{
		const auto it = m_groupItems.find(_type);
		return it == m_groupItems.end() ? nullptr : it->second;
	}
}
