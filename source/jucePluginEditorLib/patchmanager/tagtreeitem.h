#pragma once

#include "treeitem.h"
#include "types.h"

namespace jucePluginEditorLib::patchManager
{
	class TagTreeItem : public TreeItem
	{
	public:
		TagTreeItem(PatchManagerUiJuce& _pm, GroupType _type, const std::string& _tag);

		auto getGroupType() const { return m_group; }

		const auto& getTag() const { return m_tag; }

	private:
		const GroupType m_group;
		const std::string m_tag;
	};
}
