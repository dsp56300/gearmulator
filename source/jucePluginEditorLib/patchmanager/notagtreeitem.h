#pragma once
#include "treeitem.h"

namespace jucePluginEditorLib::patchManager
{
	class NoTagTreeItem : public TreeItem
	{
	public:
		NoTagTreeItem(PatchManager& _pm, pluginLib::patchDB::TagType _type, const std::string& _title);

		void onParentSearchChanged(const pluginLib::patchDB::SearchRequest& _parentSearchRequest) override;
		bool mightContainSubItems() override { return false; }

	private:
		pluginLib::patchDB::TagType m_tagType;
	};
}
