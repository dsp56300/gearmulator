#include "patchmanagerUiRml.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlElemTree.h"
#include "juceRmlUi/rmlHelper.h"

namespace jucePluginEditorLib::patchManagerRml
{
	PatchManagerUiRml::PatchManagerUiRml(Editor& _editor, patchManager::PatchManager& _db, juceRmlUi::RmlComponent& _comp, Rml::Element* _root, const std::initializer_list<patchManager::GroupType>& _groupTypes)
		: PatchManagerUi(_editor, _db)
		, m_rmlComponent(_comp)
		, m_treeDS(*this, juceRmlUi::helper::findChildT<juceRmlUi::ElemTree>(_root, "pm-tree-datasources"))
		, m_treeTags(*this, juceRmlUi::helper::findChildT<juceRmlUi::ElemTree>(_root, "pm-tree-tags"))
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
	}

	bool PatchManagerUiRml::setSelectedDataSource(const pluginLib::patchDB::DataSourceNodePtr& _ds)
	{
		return false;
	}

	void PatchManagerUiRml::setSelectedPatch(const pluginLib::patchDB::PatchPtr& _patch)
	{
	}

	void PatchManagerUiRml::setSelectedPatches(const std::set<pluginLib::patchDB::PatchPtr>& _patches)
	{
	}

	bool PatchManagerUiRml::setSelectedPatches(const std::set<pluginLib::patchDB::PatchKey>& _patches)
	{
		return false;
	}

	void PatchManagerUiRml::setCustomSearch(pluginLib::patchDB::SearchHandle _sh)
	{
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
}
