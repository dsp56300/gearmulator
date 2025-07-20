#pragma once

#include "treeNode.h"
#include "jucePluginEditorLib/patchmanager/types.h"

#include "jucePluginLib/patchdb/patchdbtypes.h"

namespace juceRmlUi
{
	class ColorPicker;
}

namespace jucePluginEditorLib::patchManagerRml
{
	struct TagNode : juceRmlUi::TreeNode
	{
		explicit TagNode(PatchManagerUiRml& _pm, juceRmlUi::Tree& _tree, patchManager::GroupType _groupType, pluginLib::patchDB::Tag _tag)
			: TreeNode(_tree)
			, m_patchManager(_pm)
			, m_group(_groupType)
			, m_tag(std::move(_tag))
		{
		}

		const auto& getGroup() const { return m_group; }
		const auto& getTag() const { return m_tag; }

		auto& getPatchManager() const { return m_patchManager; }

	private:
		PatchManagerUiRml& m_patchManager;
		patchManager::GroupType m_group;
		pluginLib::patchDB::Tag m_tag;
	};

	class TagsTree;

	class TagTreeElem : public TreeElem
	{
	public:
		TagTreeElem(Tree& _tree, const std::string& _rmlElemTag);

		void setNode(const juceRmlUi::TreeNodePtr& _node) override;

		void onParentSearchChanged(const pluginLib::patchDB::SearchRequest& _parentSearchRequest) override;

		void onRightClick(const Rml::Event& _event) override;

		bool canDropPatchList(const Rml::Event& _event, const Rml::Element* _source, const std::vector<pluginLib::patchDB::PatchPtr>& _patches) const override;
		void dropPatches(const Rml::Event& _event, const patchManager::SavePatchDesc* _data, const std::vector<pluginLib::patchDB::PatchPtr>& _patches) override;

		static void modifyTags(patchManager::PatchManager& _pm, pluginLib::patchDB::TagType _type, const std::string& _tag, const std::vector<pluginLib::patchDB::PatchPtr>& _patches);

		pluginLib::patchDB::Tag getTag() const;
		pluginLib::patchDB::TagType getTagType() const;
		pluginLib::patchDB::Color getColor() const;

	private:
		std::unique_ptr<juceRmlUi::ColorPicker> m_colorPicker;
	};
}
