#include "tagtreeitem.h"

#include "patchmanager.h"

namespace jucePluginEditorLib::patchManager
{
	TagTreeItem::TagTreeItem(PatchManager& _pm, const GroupType _type, const std::string& _tag) : TreeItem(_pm, _tag), m_group(_type), m_tag(_tag)
	{
		const auto tagType = toTagType(_type);

		if(tagType != pluginLib::patchDB::TagType::Invalid)
		{
			pluginLib::patchDB::SearchRequest sr;
			sr.tags.add(tagType, _tag);
			search(std::move(sr));
		}
	}

	bool TagTreeItem::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails)
	{
		return TreeItem::isInterestedInDragSource(dragSourceDetails) && hasSearch();
	}

	void TagTreeItem::patchDropped(const pluginLib::patchDB::PatchPtr& _patch)
	{
		const auto tagType = toTagType(getGroupType());

		if(tagType != pluginLib::patchDB::TagType::Invalid)
		{
			pluginLib::patchDB::TypedTags tags;
			if (juce::ModifierKeys::currentModifiers.isShiftDown())
				tags.addRemoved(tagType, getTag());
			else
				tags.add(tagType, getTag());

			getPatchManager().modifyTags(_patch, tags);
		}
		TreeItem::patchDropped(_patch);
	}
}
