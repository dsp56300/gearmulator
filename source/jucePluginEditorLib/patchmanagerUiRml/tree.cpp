#include "tree.h"

#include "patchmanagerUiRml.h"
#include "jucePluginEditorLib/patchmanager/patchmanager.h"

namespace jucePluginEditorLib::patchManagerRml
{
	Tree::Tree(PatchManagerUiRml& _pm, juceRmlUi::ElemTree* _tree)
	: m_tree(_tree)
	, m_pm(_pm)
	, m_instancerGroup(*this)
	, m_instancerDatasource(*this)
	, m_instancerTag(*this)
	{
		_tree->setNodeInstancerCallback([this](const std::shared_ptr<juceRmlUi::TreeNode>& _node) -> Rml::ElementInstancer*
		{
			if (dynamic_cast<GroupNode*>(_node.get()))
				return &m_instancerGroup;
			if (dynamic_cast<TagNode*>(_node.get()))
				return &m_instancerTag;
			return &m_instancerDatasource;
		});

		_tree->getTree().evNodeSelectionChanged.addListener([this](const std::shared_ptr<juceRmlUi::TreeNode>& _treeNode, const bool& _selected)
		{
			if (m_tree->getTree().isMultiSelectEnabled())
			{
				if (_selected)
					m_pm.addSelectedItem(*this, _treeNode);
				else
					m_pm.removeSelectedItem(*this, _treeNode);
			}
			else if (_selected)
				m_pm.setSelectedItem(*this, _treeNode);
		});
	}

	Tree::~Tree()
	{
		m_tree->getTree().getRoot()->clear();
	}

	patchManager::PatchManager& Tree::getDB() const
	{
		return m_pm.getDB();
	}

	bool Tree::addGroup(patchManager::GroupType _type)
	{
		if (_type == patchManager::GroupType::Invalid)
			return false;
		auto it = m_groupItems.find(_type);
		if (it != m_groupItems.end())
			return false;

		auto& tree = getTree()->getTree();
		auto item = std::make_shared<GroupNode>(m_pm, tree, _type); 

		m_groupItems.insert({ _type, item });

		tree.getRoot()->addChild(item);
		return true;
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

	void Tree::onParentSearchChanged(const pluginLib::patchDB::SearchRequest& _searchRequest) const
	{
		for (const auto& [groupType, node] : m_groupItems)
		{
			auto* elem = dynamic_cast<TreeElem*>(node->getElement());
			elem->setParentSearchRequest(_searchRequest);
		}
	}

	bool Tree::setSelectedDatasource(const pluginLib::patchDB::DataSourceNodePtr& _ds)
	{
		if (!m_tree)
			return false;
		auto item = getItemT<GroupNode>(patchManager::GroupType::DataSources);
		if (!item)
			return false;
		return item->setSelectedDatasource(_ds);
	}

	GroupItemPtr Tree::getGroupItem(const patchManager::GroupType _group)
	{
		auto it = m_groupItems.find(_group);
		if (it != m_groupItems.end())
			return it->second;
		return {};
	}

	void Tree::setFilter(const std::string& _filter) const
	{
		for (const auto& it : m_groupItems)
			it.second->setFilter(_filter);
	}

	GroupItemPtr Tree::getItem(const patchManager::GroupType _group)
	{
		auto it = m_groupItems.find(_group);
		if (it != m_groupItems.end())
			return it->second;
		return nullptr;
	}

	void Tree::updateDataSources()
	{
		auto itemDs = getItemT<GroupNode>(patchManager::GroupType::DataSources);
		auto itemLocalStorage = getItemT<GroupNode>(patchManager::GroupType::LocalStorage);
		auto itemFactory = getItemT<GroupNode>(patchManager::GroupType::Factory);

		if (!itemDs || !itemLocalStorage)
			return;

		std::vector<pluginLib::patchDB::DataSourceNodePtr> allDataSources;

		std::vector<pluginLib::patchDB::DataSourceNodePtr> readOnlyDataSources;
		std::vector<pluginLib::patchDB::DataSourceNodePtr> storageDataSources;
		std::vector<pluginLib::patchDB::DataSourceNodePtr> factoryDataSources;

		getDB().getDataSources(allDataSources);

		readOnlyDataSources.reserve(allDataSources.size());
		storageDataSources.reserve(allDataSources.size());
		factoryDataSources.reserve(allDataSources.size());

		for (const auto& ds : allDataSources)
		{
			if (ds->type == pluginLib::patchDB::SourceType::LocalStorage)
				storageDataSources.push_back(ds);
			else if (ds->type == pluginLib::patchDB::SourceType::Rom)
				factoryDataSources.push_back(ds);
			else
				readOnlyDataSources.push_back(ds);
		}

		itemDs->updateFromDataSources(readOnlyDataSources);
		itemLocalStorage->updateFromDataSources(storageDataSources);

		if (itemFactory)
			itemFactory->updateFromDataSources(factoryDataSources);
	}

	void Tree::updateTags(const patchManager::GroupType& _type)
	{
		const auto tagType = patchManager::toTagType(_type);
		if (tagType == pluginLib::patchDB::TagType::Invalid)
			return;
		auto item = getItem(_type);
		if (!item)
			return;
		std::set<pluginLib::patchDB::Tag> tags;
		getDB().getTags(tagType, tags);
		item->updateFromTags(tags);
	}

	void Tree::updateTags(const pluginLib::patchDB::TagType& _tag)
	{
		const auto groupType = patchManager::toGroupType(_tag);
		if (groupType == patchManager::GroupType::Invalid)
			return;
		updateTags(groupType);
	}
}
