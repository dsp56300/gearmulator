#include "tree.h"

#include <set>

#include "grouptreeitem.h"
#include "patchmanager.h"
#include "roottreeitem.h"
#include "treeitem.h"

namespace jucePluginEditorLib::patchManager
{
	Tree::Tree(PatchManager& _patchManager) : m_patchManager(_patchManager)
	{
//		setColour(backgroundColourId, juce::Colour(0xff999999));
		setColour(backgroundColourId, juce::Colour());
		setColour(linesColourId, juce::Colour(0xffffffff));
		setColour(dragAndDropIndicatorColourId, juce::Colour(0xff00ff00));
		setColour(selectedItemBackgroundColourId, juce::Colour(0xffaaaaaa));
//		setColour(oddItemsColourId, juce::Colour(0xff333333));
//		setColour(evenItemsColourId, juce::Colour(0xff555555));

		auto *rootItem = new RootTreeItem(m_patchManager);
		setRootItem(rootItem);
		setRootItemVisible(false);

		addGroup(GroupType::DataSources);
		addGroup(GroupType::Categories);
		addGroup(GroupType::Tags);
		addGroup(GroupType::Favourites);

		getViewport()->setScrollBarsShown(true, true);
	}

	Tree::~Tree()
	{
		deleteRootItem();
	}

	void Tree::updateDataSources()
	{
		auto* item = getItem(GroupType::DataSources);
		if (!item)
			return;

		std::vector<std::shared_ptr<pluginLib::patchDB::DataSource>> dataSources;
		m_patchManager.getDataSources(dataSources);

		item->updateFromDataSources(dataSources);
	}

	void Tree::updateCategories()
	{
		auto* item = getItem(GroupType::Categories);
		if (!item)
			return;

		std::set<pluginLib::patchDB::Tag> categories;
		m_patchManager.getCategories(categories);

		item->updateFromTags(categories);
	}

	void Tree::updateTags()
	{
		auto* item = getItem(GroupType::Tags);
		if (!item)
			return;
		std::set<pluginLib::patchDB::Tag> tags;
		m_patchManager.getTags(tags);

		item->updateFromTags(tags);
	}

	void Tree::processDirty(const pluginLib::patchDB::Dirty& _dirty)
	{
		if (_dirty.dataSources)
			updateDataSources();

		if (_dirty.categories)
			updateCategories();

		if (_dirty.tags)
			updateTags();

		if (!_dirty.searches.empty())
		{
			for (const auto& it : m_groupItems)
				it.second->processDirty(_dirty.searches);
		}
	}

	void Tree::paint(juce::Graphics& g)
	{
		// we don't want a background
		//TreeView::paint(g);
	}

	void Tree::addGroup(const GroupType _type)
	{
		auto* groupItem = new GroupTreeItem(m_patchManager, _type);
		getRootItem()->addSubItem(groupItem);
		m_groupItems.insert({ _type, groupItem });
	}

	GroupTreeItem* Tree::getItem(const GroupType _type)
	{
		const auto it = m_groupItems.find(_type);
		return it == m_groupItems.end() ? nullptr : it->second;
	}
}
