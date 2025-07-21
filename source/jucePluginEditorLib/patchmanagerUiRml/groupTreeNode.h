#pragma once

#include "treeNode.h"

namespace juce
{
	class FileChooser;
}

namespace jucePluginEditorLib::patchManagerRml
{
	struct TagNode;
	struct DatasourceNode;
	class DatasourceTree;
}

namespace jucePluginEditorLib::patchManager
{
	enum class GroupType;
}

namespace jucePluginEditorLib::patchManagerRml
{
	struct GroupNode : juceRmlUi::TreeNode
	{
		using DatasourceItemPtr = std::shared_ptr<DatasourceNode>;
		using TagItemPtr = std::shared_ptr<TagNode>;

		explicit GroupNode(PatchManagerUiRml& _pm, juceRmlUi::Tree& _tree, const patchManager::GroupType _type)
			: TreeNode(_tree)
			, m_patchManager(_pm)
			, m_type(_type)
		{
		}

		void updateFromDataSources(const std::vector<pluginLib::patchDB::DataSourceNodePtr>& _dataSources);
		void updateFromTags(const std::set<pluginLib::patchDB::Tag>& _tags);

		DatasourceItemPtr createItemForDataSource(const pluginLib::patchDB::DataSourceNodePtr& _ds);
		void removeDataSource(const pluginLib::patchDB::DataSourceNodePtr& _ds);

		TagItemPtr createSubItem(const pluginLib::patchDB::Tag& _tag);

		void validateParent(const pluginLib::patchDB::DataSourceNodePtr& _ds, const DatasourceItemPtr& _node);
		void validateParent(const TagItemPtr& _item);

		bool needsParentItem(const pluginLib::patchDB::DataSourceNodePtr& _ds) const;

		bool match(const DatasourceItemPtr& _ds) const;
		bool match(const TagItemPtr& _item) const;
		bool match(const std::string& _text) const;

		patchManager::GroupType getGroupType() const { return m_type; }

		void processDirty(const std::set<pluginLib::patchDB::SearchHandle>& _searches);

		void onParentSearchChanged(const pluginLib::patchDB::SearchRequest& _searchRequest);

		bool setSelectedDatasource(const pluginLib::patchDB::DataSourceNodePtr& _ds);

	private:
		PatchManagerUiRml& m_patchManager;
		patchManager::GroupType m_type;

		std::map<pluginLib::patchDB::DataSourceNodePtr, DatasourceItemPtr> m_itemsByDataSource;
		std::map<pluginLib::patchDB::Tag, TagItemPtr> m_itemsByTag;

		std::string m_filter;
	};

	class GroupTreeElem : public TreeElem
	{
	public:
		explicit GroupTreeElem(Tree& _tree, const Rml::String& _tag);
		void setNode(const juceRmlUi::TreeNodePtr& _node) override;

		void onParentSearchChanged(const pluginLib::patchDB::SearchRequest& _parentSearchRequest) override;

		void onRightClick(const Rml::Event& _event) override;

		bool canDrop(const Rml::Event& _event, const DragSource* _source) const override;
		bool canDropFiles(const Rml::Event& _event, const std::vector<std::string>& _files) const override;

		void dropFiles(const Rml::Event& _event, const juceRmlUi::FileDragData* _data, const std::vector<std::string>& _files) override;
		void dropPatches(const Rml::Event& _event, const patchManager::SavePatchDesc* _data, const std::vector<pluginLib::patchDB::PatchPtr>& _patches) override;
		bool canDropPatchList(const Rml::Event& _event, const Rml::Element* _source, const std::vector<pluginLib::patchDB::PatchPtr>& _patches) const override;

		patchManager::GroupType getGroupType() const;

	private:
		std::unique_ptr<juce::FileChooser> m_chooser;
	};
}
