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
		setColour(backgroundColourId, juce::Colour(0xff444444));
//		setColour(backgroundColourId, juce::Colour(0));
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

		std::vector<pluginLib::patchDB::DataSourceNodePtr> dataSources;
		m_patchManager.getDataSources(dataSources);

		item->updateFromDataSources(dataSources);
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
		// if we don't want a background, skip this call
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
