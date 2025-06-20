#include "tagTreeNode.h"

#include "tagsTree.h"

namespace jucePluginEditorLib::patchManagerRml
{
	TagTreeElem::TagTreeElem(Tree& _tree, const std::string& _rmlElemTag) : TreeElem(_tree, _rmlElemTag)
	{
	}

	void TagTreeElem::setNode(const juceRmlUi::TreeNodePtr& _node)
	{
		TreeElem::setNode(_node);

		auto* tagItem = dynamic_cast<TagNode*>(_node.get());
		if (!tagItem)
			return;

		setName(tagItem->getTag());

		const auto tagType = toTagType(tagItem->getGroup());

		if (tagType == pluginLib::patchDB::TagType::Favourites)
		{
			pluginLib::patchDB::SearchRequest sr;
			sr.tags.add(tagType, tagItem->getTag());

			search(std::move(sr));
		}
	}

	void TagTreeElem::onParentSearchChanged(const pluginLib::patchDB::SearchRequest& _parentSearchRequest)
	{
		TreeElem::onParentSearchChanged(_parentSearchRequest);

		const auto* tagItem = dynamic_cast<TagNode*>(getNode().get());
		if (!tagItem)
			return;

		const auto tagType = toTagType(tagItem->getGroup());

		if(tagType == pluginLib::patchDB::TagType::Invalid)
			return;

		pluginLib::patchDB::SearchRequest sr = _parentSearchRequest;
		sr.tags.add(tagType, tagItem->getTag());

		search(std::move(sr));
	}
}
