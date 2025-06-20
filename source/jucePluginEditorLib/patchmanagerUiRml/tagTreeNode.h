#pragma once

#include "treeNode.h"
#include "jucePluginEditorLib/patchmanager/types.h"

#include "jucePluginLib/patchdb/patchdbtypes.h"

namespace jucePluginEditorLib::patchManagerRml
{
	struct TagNode : juceRmlUi::TreeNode
	{
		explicit TagNode(juceRmlUi::Tree& _tree, patchManager::GroupType _groupType, pluginLib::patchDB::Tag _tag)
			: TreeNode(_tree)
			, m_group(_groupType)
			, m_tag(std::move(_tag))
		{
		}

		const auto& getGroup() const { return m_group; }
		const auto& getTag() const { return m_tag; }

	private:
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
	};
}
