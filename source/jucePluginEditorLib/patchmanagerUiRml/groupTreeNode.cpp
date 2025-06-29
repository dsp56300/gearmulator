#include "groupTreeNode.h"

#include "patchmanagerUiRml.h"

#include "jucePluginEditorLib/patchmanager/search.h"
#include "jucePluginEditorLib/patchmanager/types.h"

namespace jucePluginEditorLib::patchManagerRml
{
	void GroupNode::updateFromDataSources(const std::vector<pluginLib::patchDB::DataSourceNodePtr>& _dataSources)
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

			const auto oldCount = size();

			createItemForDataSource(d);

			if (size() == 1 && oldCount == 0)
				setOpened(true);
		}
	}

	void GroupNode::updateFromTags(const std::set<pluginLib::patchDB::Tag>& _tags)
	{
		for(auto it = m_itemsByTag.begin(); it != m_itemsByTag.end();)
		{
			const auto tag = it->first;
			const auto& item = it->second;

			if(_tags.find(tag) == _tags.end())
			{
				item->removeFromParent();
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

			const auto oldNumSubItems = size();

			createSubItem(tag);

			if (size() == 1 && oldNumSubItems == 0)
				setOpened(true);
		}
	}

	std::shared_ptr<DatasourceNode> GroupNode::createItemForDataSource(const pluginLib::patchDB::DataSourceNodePtr& _ds)
	{
		const auto it = m_itemsByDataSource.find(_ds);

		if (it != m_itemsByDataSource.end())
			return it->second;

		auto node = std::make_shared<DatasourceNode>(getTree(), _ds);

		m_itemsByDataSource.insert({ _ds, node });

		validateParent(_ds, node);

		return node;
	}

	void GroupNode::removeDataSource(const pluginLib::patchDB::DataSourceNodePtr& _ds)
	{
		auto it = m_itemsByDataSource.find(_ds);
		if (it == m_itemsByDataSource.end())
			return;
		it->second->removeFromParent();
	}

	GroupNode::TagItemPtr GroupNode::createSubItem(const pluginLib::patchDB::Tag& _tag)
	{
		auto item = std::make_shared<TagNode>(m_patchManager, getTree(), m_type, _tag);

		validateParent(item);

		m_itemsByTag.insert({ _tag, item });

		auto childElem = dynamic_cast<patchManagerRml::TreeElem*>(item->getElement());
		auto* myElem = dynamic_cast<patchManagerRml::TreeElem*>(getElement());

		childElem->onParentSearchChanged(myElem->getParentSearchRequest());

		return item;
	}

	void GroupNode::validateParent(const pluginLib::patchDB::DataSourceNodePtr& _ds, const std::shared_ptr<DatasourceNode>& _node)
	{
		juceRmlUi::TreeNodePtr parentNeeded;

		if (needsParentItem(_ds))
		{
			parentNeeded = createItemForDataSource(_ds->getParent());
		}
		else if (_ds->type == pluginLib::patchDB::SourceType::Folder && !m_filter.empty())
		{
			parentNeeded = getTree().getRoot();
		}
		else if (match(_node))
		{
			parentNeeded = shared_from_this();
		}

		_node->setParent(parentNeeded);
	}

	void GroupNode::validateParent(const TagItemPtr& _item)
	{
		if (match(_item))
			_item->setParent(shared_from_this());
		else
			_item->setParent(getTree().getRoot());
	}

	bool GroupNode::needsParentItem(const pluginLib::patchDB::DataSourceNodePtr& _ds) const
	{
		if (!m_filter.empty())
			return false;
		return _ds->hasParent() && _ds->origin != pluginLib::patchDB::DataSourceOrigin::Manual;
	}

	bool GroupNode::match(const DatasourceItemPtr& _ds) const
	{
		if (m_filter.empty())
			return true;
		return match(_ds->getText());
	}

	bool GroupNode::match(const TagItemPtr& _item) const
	{
		if (m_filter.empty())
			return true;
		return match(_item->getTag());
	}

	bool GroupNode::match(const std::string& _text) const
	{
		if (m_filter.empty())
			return true;
		const auto t = patchManager::Search::lowercase(_text);
		return t.find(m_filter) != std::string::npos;
	}

	void GroupNode::processDirty(const std::set<pluginLib::patchDB::SearchHandle>& _searches)
	{
		for (const auto& it : m_itemsByDataSource)
		{
			auto* elem = it.second->getElement();
			auto* treeNode = dynamic_cast<patchManagerRml::TreeElem*>(elem);
			if (treeNode)
				treeNode->processDirty(_searches);
		}
		for (const auto& it : m_itemsByTag)
		{
			auto* elem = it.second->getElement();
			auto* treeNode = dynamic_cast<patchManagerRml::TreeElem*>(elem);
			if (treeNode)
				treeNode->processDirty(_searches);
		}
	}

	void GroupNode::onParentSearchChanged(const pluginLib::patchDB::SearchRequest& _searchRequest)
	{
		for (const auto& [ds, item] : m_itemsByDataSource)
		{
			auto* elem = dynamic_cast<TreeElem*>(item->getElement());
			elem->setParentSearchRequest(_searchRequest);
		}

		for (const auto& [tag, item] : m_itemsByTag)
		{
			auto* elem = dynamic_cast<TreeElem*>(item->getElement());
			elem->setParentSearchRequest(_searchRequest);
		}
	}

	GroupTreeElem::GroupTreeElem(Tree& _tree, const Rml::String& _tag) : TreeElem(_tree, _tag)
	{
	}

	void GroupTreeElem::setNode(const juceRmlUi::TreeNodePtr& _node)
	{
		TreeElem::setNode(_node);

		auto* item = dynamic_cast<GroupNode*>(_node.get());

		if (!item)
			return;
		const auto groupType = item->getGroupType();
		if (groupType == patchManager::GroupType::Invalid)
			return;
		auto name = getPatchManager().getGroupName(groupType);

		setName(name);
	}

	void GroupTreeElem::onParentSearchChanged(const pluginLib::patchDB::SearchRequest& _parentSearchRequest)
	{
		TreeElem::onParentSearchChanged(_parentSearchRequest);

		auto node = dynamic_cast<GroupNode*>(getNode().get());

		node->onParentSearchChanged(_parentSearchRequest);
	}
}
