#pragma once

#include "treeitem.h"
#include "types.h"

namespace jucePluginEditorLib::patchManager
{
	class TagTreeItem : public TreeItem
	{
	public:
		TagTreeItem(PatchManager& _pm, GroupType _type, const std::string& _tag);

		bool mightContainSubItems() override
		{
			return false;
		}

		auto getGroupType() const { return m_group; }

		void processSearchUpdated(const pluginLib::patchDB::Search& _search) override;

	private:
		const GroupType m_group;
		const std::string m_tag;
	};
}
