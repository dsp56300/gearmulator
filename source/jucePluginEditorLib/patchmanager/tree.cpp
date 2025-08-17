#include "tree.h"

#include <set>

#include "grouptreeitem.h"
#include "patchmanager.h"
#include "treeitem.h"
#include "defaultskin.h"
#include "patchmanageruijuce.h"

#include "juceUiLib/uiObject.h"

#include "../pluginEditor.h"

namespace jucePluginEditorLib::patchManager
{
	Tree::Tree(PatchManagerUiJuce& _patchManager) : m_patchManager(_patchManager)
	{
	}

	Tree::~Tree()
	{
		deleteRootItem();
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
		m_patchManager.getDB().getTags(tagType, tags);
		item->updateFromTags(tags);
	}

	void Tree::updateTags(const pluginLib::patchDB::TagType _type)
	{
		const auto groupType = toGroupType(_type);
		if (groupType == GroupType::Invalid)
			return;
		updateTags(groupType);
	}

	bool Tree::keyPressed(const juce::KeyPress& _key)
	{
		if(_key.getKeyCode() == juce::KeyPress::F2Key)
		{
			// edit the currently selected item
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
		const auto it = m_groupItems.find(toGroupType(_ds.type));
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
		addGroup(_type, toString(_type));
	}

	GroupTreeItem* Tree::getItem(const GroupType _type)
	{
		const auto it = m_groupItems.find(_type);
		return it == m_groupItems.end() ? nullptr : it->second;
	}
}
