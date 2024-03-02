#include "tagstree.h"

#include "notagtreeitem.h"

namespace jucePluginEditorLib::patchManager
{
	TagsTree::TagsTree(PatchManager& _pm) : Tree(_pm)
	{
		addGroup(GroupType::Categories);
		m_uncategorized = new NoTagTreeItem(_pm, pluginLib::patchDB::TagType::Category, "Uncategorized");
		getRootItem()->addSubItem(m_uncategorized);
		addGroup(GroupType::Tags);

		setMultiSelectEnabled(true);
	}

	void TagsTree::onParentSearchChanged(const pluginLib::patchDB::SearchRequest& _searchRequest)
	{
		Tree::onParentSearchChanged(_searchRequest);

		m_uncategorized->onParentSearchChanged(_searchRequest);
	}

	void TagsTree::processDirty(const pluginLib::patchDB::Dirty& _dirty)
	{
		Tree::processDirty(_dirty);

		m_uncategorized->processDirty(_dirty.searches);
	}
}
