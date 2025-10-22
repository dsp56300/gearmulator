#pragma once

#include "datasourceTreeNode.h"
#include "groupTreeNode.h"
#include "tagTreeNode.h"

#include "RmlUi/Core/ElementInstancer.h"

namespace jucePluginEditorLib::patchManager
{
	class PatchManager;
}

namespace juceRmlUi
{
	class ElemTree;
}

namespace jucePluginEditorLib::patchManagerRml
{
	class PatchManagerUiRml;
	class Tree;

	using GroupItemPtr = std::shared_ptr<GroupNode>;

	template<typename TTree, typename TNode>
	class TreeNodeInstancer : public Rml::ElementInstancer
	{
	public:
		explicit TreeNodeInstancer(TTree& _tree) : m_tree(_tree) {}

		Rml::ElementPtr InstanceElement(Rml::CoreInstance& core_instance, Rml::Element*/* _parent*/, const Rml::String& _tag, const Rml::XMLAttributes&/* _attributes*/) override
		{
			return Rml::ElementPtr(new TNode(m_tree, core_instance, _tag));
		}
		void ReleaseElement(Rml::CoreInstance&, Rml::Element* _element) override
		{
			delete _element;
		}

	private:
		TTree& m_tree;
	};

	class Tree
	{
	public:
		explicit Tree(PatchManagerUiRml& _pm, juceRmlUi::ElemTree* _tree);
		virtual ~Tree();

		auto& getPatchManager() const { return m_pm; }	

		patchManager::PatchManager& getDB() const;

		auto* getTree() const { return m_tree; }

		bool addGroup(patchManager::GroupType _type);

		void processDirty(const pluginLib::patchDB::Dirty& _dirty);

		void onParentSearchChanged(const pluginLib::patchDB::SearchRequest& _searchRequest) const;

		bool setSelectedDatasource(const pluginLib::patchDB::DataSourceNodePtr& _ds);

		GroupItemPtr getGroupItem(patchManager::GroupType _group);

		void setFilter(const std::string& _filter) const;

		std::shared_ptr<DatasourceNode> getItem(const pluginLib::patchDB::DataSource& _ds) const;

	private:
		GroupItemPtr getItem(patchManager::GroupType _group);

		template<typename T>
		T* getItemT(const patchManager::GroupType _group)
		{
			const auto item = getItem(_group);
			if (!item)
				return nullptr;
			return dynamic_cast<T*>(item.get());
		}

		void updateDataSources();
		void updateTags(const patchManager::GroupType& _type);
		void updateTags(const pluginLib::patchDB::TagType& _tag);

		TreeNodeInstancer<Tree, GroupTreeElem> m_instancerGroup;
		TreeNodeInstancer<Tree, DatasourceTreeElem> m_instancerDatasource;
		TreeNodeInstancer<Tree, TagTreeElem> m_instancerTag;

		juceRmlUi::ElemTree* const m_tree;

		PatchManagerUiRml& m_pm;

		std::unordered_map<patchManager::GroupType, GroupItemPtr> m_groupItems;
	};
}
