#include "groupTreeNode.h"

#include "patchmanagerUiRml.h"

#include "jucePluginEditorLib/patchmanager/patchmanager.h"
#include "jucePluginEditorLib/patchmanager/search.h"
#include "jucePluginEditorLib/patchmanager/types.h"

#include "juceRmlUi/rmlHelper.h"
#include "juceRmlUi/rmlInplaceEditor.h"
#include "juceRmlUi/rmlMenu.h"

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

			if (tag.empty())
			{
				// never delete the Uncategorized tag
				++it;
				continue;
			}

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

		auto childElem = dynamic_cast<TreeElem*>(item->getElement());
		auto* myElem = dynamic_cast<TreeElem*>(getElement());

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

		_node->setParent(parentNeeded, [](const juceRmlUi::TreeNodePtr& _a, const juceRmlUi::TreeNodePtr& _b)
		{
			auto* a = dynamic_cast<DatasourceNode*>(_a.get());
			auto* b = dynamic_cast<DatasourceNode*>(_b.get());

			if (!a || !b)
				return false;

			return a->getText() < b->getText();
		});
	}

	void GroupNode::validateParent(const TagItemPtr& _item)
	{
		auto compareNodes = [](const juceRmlUi::TreeNodePtr& _a, const juceRmlUi::TreeNodePtr& _b)
		{
			auto* a = dynamic_cast<TagNode*>(_a.get());
			auto* b = dynamic_cast<TagNode*>(_b.get());

			if (!a || !b)
				return false;

			return a->getName() < b->getName();
		};

		if (match(_item))
			_item->setParent(shared_from_this(), compareNodes);
		else
			_item->setParent(getTree().getRoot(), compareNodes);
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
			auto* treeNode = dynamic_cast<TreeElem*>(elem);
			if (treeNode)
				treeNode->processDirty(_searches);
		}
		for (const auto& it : m_itemsByTag)
		{
			auto* elem = it.second->getElement();
			auto* treeNode = dynamic_cast<TreeElem*>(elem);
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

	bool GroupNode::setSelectedDatasource(const pluginLib::patchDB::DataSourceNodePtr& _ds)
	{
		const auto it = m_itemsByDataSource.find(_ds);
		if (it == m_itemsByDataSource.end())
			return false;
		auto* node = it->second.get();
		if (!node)
			return false;
		node->setSelected(true, false);

		if (!node->isSelected())
			return false;

		node->makeVisible();

		juceRmlUi::helper::callPostFrame([node]
		{
			Rml::ScrollIntoViewOptions options;

			options.vertical = Rml::ScrollAlignment::Nearest;
			options.horizontal = Rml::ScrollAlignment::Nearest;
			options.behavior = Rml::ScrollBehavior::Smooth;
			options.parentage = Rml::ScrollParentage::All;

			node->getElement()->ScrollIntoView(options);
		});

		return true;
	}

	GroupTreeElem::GroupTreeElem(Tree& _tree, const Rml::String& _tag) : TreeElem(_tree, _tag)
	{
		SetProperty(Rml::PropertyId::Drag, Rml::Style::Drag::None);
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
		setCountEnabled(false);
	}

	void GroupTreeElem::onParentSearchChanged(const pluginLib::patchDB::SearchRequest& _parentSearchRequest)
	{
		TreeElem::onParentSearchChanged(_parentSearchRequest);

		auto node = dynamic_cast<GroupNode*>(getNode().get());

		node->onParentSearchChanged(_parentSearchRequest);
	}

	void GroupTreeElem::onRightClick(const Rml::Event& _event)
	{
		TreeElem::onRightClick(_event);

		juceRmlUi::Menu menu;

		auto node = dynamic_cast<GroupNode*>(getNode().get());

		const auto groupType = node->getGroupType();
		const auto tagType = toTagType(groupType);

		if (groupType == patchManager::GroupType::DataSources)
		{
			menu.addEntry("Add folders...", [this]
				{
					m_chooser.reset(new juce::FileChooser("Select Folders"));

					m_chooser->launchAsync(
						juce::FileBrowserComponent::openMode |
						juce::FileBrowserComponent::canSelectDirectories |
						juce::FileBrowserComponent::canSelectMultipleItems
						, [this](const juce::FileChooser& _fileChooser)
						{
							for (const auto& r : _fileChooser.getResults())
							{
								const auto result = r.getFullPathName().toStdString();
								pluginLib::patchDB::DataSource ds;
								ds.type = pluginLib::patchDB::SourceType::Folder;
								ds.name = result;
								ds.origin = pluginLib::patchDB::DataSourceOrigin::Manual;
								getDB().addDataSource(ds);
							}

							m_chooser.reset();
						});
				});

			menu.addEntry("Add files...", [this]
				{
					m_chooser.reset(new juce::FileChooser("Select Files"));

					m_chooser->launchAsync(
						juce::FileBrowserComponent::openMode |
						juce::FileBrowserComponent::canSelectFiles |
						juce::FileBrowserComponent::canSelectMultipleItems,
						[this](const juce::FileChooser& _fileChooser)
						{
							for (const auto& r : _fileChooser.getResults())
							{
								const auto result = r.getFullPathName().toStdString();
								pluginLib::patchDB::DataSource ds;
								ds.type = pluginLib::patchDB::SourceType::File;
								ds.name = result;
								ds.origin = pluginLib::patchDB::DataSourceOrigin::Manual;
								getDB().addDataSource(ds);
							}

							m_chooser.reset();
						});
				});
		}
		else if (groupType == patchManager::GroupType::LocalStorage)
		{
			menu.addEntry("Create...", [this]
				{
					new juceRmlUi::InplaceEditor(this, "Enter name...",
						[this](const std::string& _newName)
						{
							pluginLib::patchDB::DataSource ds;

							ds.name = _newName;
							ds.type = pluginLib::patchDB::SourceType::LocalStorage;
							ds.origin = pluginLib::patchDB::DataSourceOrigin::Manual;
							ds.timestamp = std::chrono::system_clock::now();

							getDB().addDataSource(ds);
						});
				});
		}
		if (tagType != pluginLib::patchDB::TagType::Invalid)
		{
			menu.addEntry("Add...", [this, tagType]
				{
					new juceRmlUi::InplaceEditor(this, "Enter name...",
						[this, tagType](const std::string& _newText)
						{
							if (!_newText.empty())
								getDB().addTag(tagType, _newText);
						});
				});
		}

		menu.runModal(_event);
	}
}
