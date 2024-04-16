#include "notagtreeitem.h"

namespace jucePluginEditorLib::patchManager
{
	NoTagTreeItem::NoTagTreeItem(PatchManager& _pm, const pluginLib::patchDB::TagType _type, const std::string& _title) : TreeItem(_pm, _title), m_tagType(_type)
	{
		onParentSearchChanged({});
	}

	void NoTagTreeItem::onParentSearchChanged(const pluginLib::patchDB::SearchRequest& _parentSearchRequest)
	{
		TreeItem::onParentSearchChanged(_parentSearchRequest);
		auto req = _parentSearchRequest;
		req.noTagOfType.insert(m_tagType);
		search(std::move(req));
	}
}
