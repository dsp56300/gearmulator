#pragma once

#include "datasourceTree.h"
#include "grid.h"
#include "info.h"
#include "list.h"
#include "searchlist.h"
#include "searchtree.h"
#include "status.h"
#include "tagsTree.h"

#include "jucePluginEditorLib/patchmanager/patchmanagerui.h"

#include "juceRmlUi/rmlElemTree.h"

namespace jucePluginEditorLib::patchManagerRml
{
	class PatchManagerDataModel;
}

namespace juceRmlUi
{
	class RmlComponent;
}

namespace Rml
{
	class Element;
}

namespace jucePluginEditorLib::patchManagerRml
{
	class PatchManagerUiRml : public patchManager::PatchManagerUi
	{
	public:
		enum class LayoutType
		{
			List,
			Grid
		};

		PatchManagerUiRml(Editor& _editor, patchManager::PatchManager& _db, juceRmlUi::RmlComponent& _comp, Rml::Element* _root, PatchManagerDataModel& _dataModel, const std::initializer_list<patchManager::GroupType>& _groupTypes);
		~PatchManagerUiRml() override = default;

		// base implementation
		void processDirty(const pluginLib::patchDB::Dirty& _dirty) override;
		bool setSelectedDataSource(const pluginLib::patchDB::DataSourceNodePtr& _ds) override;
		void setSelectedPatch(const pluginLib::patchDB::PatchPtr& _patch) override;
		void setSelectedPatches(const std::set<pluginLib::patchDB::PatchPtr>& _patches) override;
		bool setSelectedPatches(const std::set<pluginLib::patchDB::PatchKey>& _patches) override;
		void setCustomSearch(pluginLib::patchDB::SearchHandle _sh) override;
		void bringToFront() override;
		pluginLib::patchDB::SearchHandle getSearchHandle(const pluginLib::patchDB::DataSource& _ds, bool _selectTreeItem) override;
		bool createTag(patchManager::GroupType _group, const std::string& _name) override;

		std::string getGroupName(patchManager::GroupType _type) const;

		void addSelectedItem(const Tree& _tree, const juceRmlUi::TreeNodePtr& _node);
		void removeSelectedItem(const Tree& _tree, const juceRmlUi::TreeNodePtr& _node);
		void setSelectedItem(const Tree& _tree, const juceRmlUi::TreeNodePtr& _node);

		void onSelectedItemsChanged();

		void setLayout(LayoutType _layout);

		ListModel& getListModel();

		void setListStatus(uint32_t _selectedPatchCount, uint32_t _totalPatchCount);

		pluginLib::patchDB::Color getPatchColor(const pluginLib::patchDB::PatchPtr& _patch) const;

		LayoutType getLayout() const { return m_layout; }

		static Rml::Colourb toRmlColor(pluginLib::patchDB::Color _color);

	private:
		juceRmlUi::RmlComponent& m_rmlComponent;
		PatchManagerDataModel& m_dataModel;

		DatasourceTree m_treeDS;
		SearchTree m_searchTreeDS;

		TagsTree m_treeTags;
		SearchTree m_searchTreeTags;

		List m_list;
		SearchList m_searchList;
		Grid m_grid;
		SearchList m_searchGrid;

		Status m_status;
		Info m_info;

		Rml::Element* m_rootLayoutList = nullptr;
		Rml::Element* m_rootLayoutGrid = nullptr;

		std::unordered_map<patchManager::GroupType, std::string> m_customGroupNames;

		std::map<const Tree*, std::set<juceRmlUi::TreeNodePtr>> m_selectedItems;

		LayoutType m_layout = LayoutType::List;
	};
}
