#pragma once

#include "tree.h"

namespace jucePluginEditorLib::patchManager
{
	class NoTagTreeItem;

	class TagsTree : public Tree
	{
	public:
		explicit TagsTree(PatchManager& _pm);

		void onParentSearchChanged(const pluginLib::patchDB::SearchRequest& _searchRequest) override;
		void processDirty(const pluginLib::patchDB::Dirty& _dirty) override;

	private:
		NoTagTreeItem* m_uncategorized = nullptr;
	};
}
