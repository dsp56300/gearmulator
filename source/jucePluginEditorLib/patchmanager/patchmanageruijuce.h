#pragma once

#include "patchmanagerui.h"
#include "resizerbar.h"

namespace pluginLib::patchDB
{
	struct Dirty;
}

namespace genericUI
{
	class UiObject;
}

namespace jucePluginEditorLib
{
	class Editor;
}

namespace jucePluginEditorLib::patchManager
{
	enum class GroupType;
	class Grid;
	class List;
	class Status;
	class TreeItem;
	class SearchTree;
	class SearchList;
	class Info;
	class ListModel;
	class Tree;

	class PatchManagerUiJuce : public PatchManagerUi, public juce::Component, public juce::ChangeListener
	{
	public:
		enum class LayoutType
		{
			List,
			Grid
		};

		PatchManagerUiJuce(Editor& _editor, PatchManager& _db, Component* _root, const std::initializer_list<GroupType>& _groupTypes);
		~PatchManagerUiJuce() override;

		void resized() override;
		void paint(juce::Graphics& _g) override;

		void setListStatus(uint32_t _selected, uint32_t _total) const;

		std::shared_ptr<genericUI::UiObject> getTemplate(const std::string& _name) const;

		void addSelectedItem(Tree* _tree, const TreeItem* _item);
		void removeSelectedItem(Tree* _tree, const TreeItem* _item);

		juce::Colour getResizerBarColor() const;

		ListModel* getListModel() const;

		LayoutType getLayout() const { return m_layout; }
		void setLayout(LayoutType _layout);
		bool setGridLayout128();

		void setSelectedItem(Tree* _tree, const TreeItem* _item);
		
		bool setSelectedDataSource(const pluginLib::patchDB::DataSourceNodePtr& _ds) override;
		void setSelectedPatch(const pluginLib::patchDB::PatchPtr& _patch) override;
		void setSelectedPatches(const std::set<pluginLib::patchDB::PatchPtr>& _patches) override;
		bool setSelectedPatches(const std::set<pluginLib::patchDB::PatchKey>& _patches) override;
		void setCustomSearch(pluginLib::patchDB::SearchHandle _sh) override;
		void bringToFront() override;

		pluginLib::patchDB::DataSourceNodePtr getSelectedDataSource() const;
		TreeItem* getSelectedDataSourceTreeItem() const;

		pluginLib::patchDB::Color getPatchColor(const pluginLib::patchDB::PatchPtr& _patch) const;

		pluginLib::patchDB::SearchHandle getSearchHandle(const pluginLib::patchDB::DataSource& _ds, bool _selectTreeItem) override;
		bool createTag(GroupType _group, const std::string& _name) override;

	private:
		void processDirty(const pluginLib::patchDB::Dirty& _dirty) const override;

		void onSelectedItemsChanged();
		void changeListenerCallback(juce::ChangeBroadcaster* _source) override;

		static void selectTreeItem(TreeItem* _item);

		Tree* m_treeDS = nullptr;
		Tree* m_treeTags = nullptr;
		List* m_list = nullptr;
		Grid* m_grid = nullptr;
		Info* m_info = nullptr;

		SearchTree* m_searchTreeDS = nullptr;
		SearchTree* m_searchTreeTags = nullptr;
		SearchList* m_searchList = nullptr;
		Status* m_status = nullptr;

		std::map<Tree*, std::set<const TreeItem*>> m_selectedItems;

		juce::StretchableLayoutManager m_stretchableManager;

		ResizerBar m_resizerBarA{*this, &m_stretchableManager, 1};
		ResizerBar m_resizerBarB{*this, &m_stretchableManager, 3};
		ResizerBar m_resizerBarC{*this, &m_stretchableManager, 5};

		LayoutType m_layout = LayoutType::List;
		bool m_firstTimeGridLayout = true;
	};
}
