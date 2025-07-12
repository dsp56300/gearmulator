#include "patchmanagerUiRml.h"

#include "patchmanagerDataModel.h"

#include "jucePluginEditorLib/pluginEditor.h"
#include "jucePluginEditorLib/pluginProcessor.h"
#include "jucePluginEditorLib/patchmanager/patchmanager.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlElemTree.h"
#include "juceRmlUi/rmlElemList.h"
#include "juceRmlUi/rmlHelper.h"

namespace jucePluginEditorLib::patchManagerRml
{
	PatchManagerUiRml::PatchManagerUiRml(Editor& _editor, patchManager::PatchManager& _db, juceRmlUi::RmlComponent& _comp, Rml::Element* _root, PatchManagerDataModel& _dataModel, const std::initializer_list<patchManager::GroupType>& _groupTypes)
		: PatchManagerUi(_editor, _db)
		, m_rmlComponent(_comp)
		, m_dataModel(_dataModel)
		, m_treeDS(*this, juceRmlUi::helper::findChildT<juceRmlUi::ElemTree>(_root, "pm-tree-datasources"))
		, m_treeTags(*this, juceRmlUi::helper::findChildT<juceRmlUi::ElemTree>(_root, "pm-tree-tags"))
		, m_list(*this, juceRmlUi::helper::findChildT<juceRmlUi::ElemList>(_root, "pm-list"))
		, m_grid(*this, juceRmlUi::helper::findChildT<juceRmlUi::ElemList>(_root, "pm-grid"))
		, m_status(_dataModel)
		, m_info(*this)
	{
		for (auto group : _groupTypes)
			m_treeDS.addGroup(group);

		m_treeTags.addGroup(patchManager::GroupType::Categories);
		// TODO: add uncategorized group
		m_treeTags.addGroup(patchManager::GroupType::Tags);
	}

	void PatchManagerUiRml::processDirty(const pluginLib::patchDB::Dirty& _dirty)
	{
		juceRmlUi::RmlInterfaces::ScopedAccess access(m_rmlComponent.getInterfaces());

		m_treeDS.processDirty(_dirty);
		m_treeTags.processDirty(_dirty);

		getListModel().processDirty(_dirty);

		m_status.setScanning(isScanning());

		m_info.processDirty(_dirty);
	}

	bool PatchManagerUiRml::setSelectedDataSource(const pluginLib::patchDB::DataSourceNodePtr& _ds)
	{
		return m_treeDS.setSelectedDatasource(_ds);
	}

	void PatchManagerUiRml::setSelectedPatch(const pluginLib::patchDB::PatchPtr& _patch)
	{
		m_info.setPatch(_patch);
	}

	void PatchManagerUiRml::setSelectedPatches(const std::set<pluginLib::patchDB::PatchPtr>& _patches)
	{
		getListModel().setSelectedPatches(_patches);
	}

	bool PatchManagerUiRml::setSelectedPatches(const std::set<pluginLib::patchDB::PatchKey>& _patches)
	{
		return getListModel().setSelectedPatches(_patches);
	}

	void PatchManagerUiRml::setCustomSearch(const pluginLib::patchDB::SearchHandle _sh)
	{
		m_treeDS.getTree()->getTree().clearSelectedNodes();
		m_treeTags.getTree()->getTree().clearSelectedNodes();
		getListModel().setContent(_sh);
	}

	void PatchManagerUiRml::bringToFront()
	{
	}

	pluginLib::patchDB::SearchHandle PatchManagerUiRml::getSearchHandle(const pluginLib::patchDB::DataSource& _ds, bool _selectTreeItem)
	{
		return pluginLib::patchDB::g_invalidSearchHandle;
	}

	bool PatchManagerUiRml::createTag(const patchManager::GroupType _group, const std::string& _name)
	{
		m_customGroupNames[_group] = _name;
		return m_treeTags.addGroup(_group);
	}

	std::string PatchManagerUiRml::getGroupName(const patchManager::GroupType _type) const
	{
		const auto it = m_customGroupNames.find(_type);
		if (it != m_customGroupNames.end())
			return it->second;
		return patchManager::toString(_type);
	}

	void PatchManagerUiRml::addSelectedItem(const Tree& _tree, const juceRmlUi::TreeNodePtr& _node)
	{
		const auto oldCount = m_selectedItems[&_tree].size();
		m_selectedItems[&_tree].insert(_node);
		const auto newCount = m_selectedItems[&_tree].size();
		if(newCount > oldCount)
			onSelectedItemsChanged();
	}

	void PatchManagerUiRml::removeSelectedItem(const Tree& _tree, const juceRmlUi::TreeNodePtr& _node)
	{
		const auto it = m_selectedItems.find(&_tree);
		if(it == m_selectedItems.end())
			return;
		if(!it->second.erase(_node))
			return;
		onSelectedItemsChanged();
	}

	void PatchManagerUiRml::setSelectedItem(const Tree& _tree, const juceRmlUi::TreeNodePtr& _node)
	{
		m_selectedItems[&_tree] = std::set{_node};

		if(&_tree == &m_treeDS)
		{
			auto* elem = dynamic_cast<TreeElem*>(_node.get()->getElement());
			m_treeTags.onParentSearchChanged(elem->getSearchRequest());
		}

		onSelectedItemsChanged();
	}

	void PatchManagerUiRml::onSelectedItemsChanged()
	{
		const auto selectedTags = m_selectedItems[&m_treeTags];

		auto selectItem = [&](const juceRmlUi::TreeNodePtr& _item)
		{
			auto* elem = dynamic_cast<TreeElem*>(_item.get()->getElement());
			if(elem->getSearchHandle() != pluginLib::patchDB::g_invalidSearchHandle)
			{
				getListModel().setContent(elem->getSearchHandle());
				return true;
			}
			return false;
		};

		if(!selectedTags.empty())
		{
			if(selectedTags.size() == 1)
			{
				if(selectItem(*selectedTags.begin()))
					return;
			}
			else
			{
				auto* elem = dynamic_cast<TreeElem*>(selectedTags.begin()->get()->getElement());
				pluginLib::patchDB::SearchRequest search = elem->getSearchRequest();
				for (const auto& selectedTag : selectedTags)
				{
					elem = dynamic_cast<TreeElem*>(selectedTag.get()->getElement());
					search.tags.add(elem->getSearchRequest().tags);
				}
				getListModel().setContent(std::move(search));
				return;
			}
		}

		const auto selectedDataSources = m_selectedItems[&m_treeDS];

		if(!selectedDataSources.empty())
		{
			const auto item = *selectedDataSources.begin();
			selectItem(item);
		}
	}

	void PatchManagerUiRml::setLayout(const LayoutType _layout)
	{
		if(m_layout == _layout)
			return;

		auto& oldModel = getListModel();

		m_layout = _layout;

		auto& newModel = getListModel();

//		m_searchList->setListModel(newModel);

		newModel.setContent(oldModel.getSearchHandle());
		newModel.setSelectedEntries(oldModel.getSelectedEntries());

//		newModel.setVisible(true);
//		oldModel.setVisible(false);
/*
		if(m_firstTimeGridLayout && _layout == LayoutType::Grid)
		{
			m_firstTimeGridLayout = false;
			setGridLayout128();
		}
		else
		{
			resized();
		}
		*/
		auto& config = getEditor().getProcessor().getConfig();
		config.setValue("pm_layout", static_cast<int>(_layout));
		config.saveIfNeeded();
	}

	ListModel& PatchManagerUiRml::getListModel()
	{
		if (m_layout == LayoutType::List)
			return m_list;
		return m_grid;
	}

	void PatchManagerUiRml::setListStatus(const uint32_t _selectedPatchCount, const uint32_t _totalPatchCount)
	{
		m_status.setListStatus(_selectedPatchCount, _totalPatchCount);
	}

	pluginLib::patchDB::Color PatchManagerUiRml::getPatchColor(const pluginLib::patchDB::PatchPtr& _patch) const
	{
		// we want to prevent that a whole list is colored with one color just because that list is based on a tag, prefer other tags instead
		pluginLib::patchDB::TypedTags ignoreTags;

		for (const auto& selectedItem : m_selectedItems)
		{
			for (const auto& item : selectedItem.second)
			{
				const auto& s = dynamic_cast<TreeElem*>(item->getElement())->getSearchRequest();
				ignoreTags.add(s.tags);
			}
		}
		return getDB().getPatchColor(_patch, ignoreTags);
	}

	Rml::Colourb PatchManagerUiRml::toRmlColor(const pluginLib::patchDB::Color _color)
	{
		if (_color == pluginLib::patchDB::g_invalidColor)
			return {0, 0, 0, 0};

		return Rml::Colourb(
			(_color >> 16) & 0xFF,
			(_color >> 8) & 0xFF,
			_color & 0xFF,
			(_color >> 24) & 0xFF);
	}
}
