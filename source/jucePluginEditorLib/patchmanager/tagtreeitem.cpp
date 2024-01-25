#include "tagtreeitem.h"

#include "patchmanager.h"

namespace jucePluginEditorLib::patchManager
{
	TagTreeItem::TagTreeItem(PatchManager& _pm, const GroupType _type, const std::string& _tag) : TreeItem(_pm, _tag), m_group(_type), m_tag(_tag)
	{
		const auto tagType = toTagType(getGroupType());

		if(tagType == pluginLib::patchDB::TagType::Favourites)
		{
			pluginLib::patchDB::SearchRequest sr;
			sr.tags.add(tagType, getTag());

			search(std::move(sr));
		}
	}

	bool TagTreeItem::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails)
	{
		return TreeItem::isInterestedInDragSource(dragSourceDetails) && hasSearch();
	}

	bool TagTreeItem::isInterestedInPatchList(const List* _list, const juce::Array<juce::var>& _indices)
	{
		return hasSearch() && toTagType(getGroupType()) != pluginLib::patchDB::TagType::Invalid;
	}

	void TagTreeItem::patchesDropped(const std::vector<pluginLib::patchDB::PatchPtr>& _patches)
	{
		const auto tagType = toTagType(getGroupType());

		if (tagType == pluginLib::patchDB::TagType::Invalid)
			return;

		pluginLib::patchDB::TypedTags tags;
		if (juce::ModifierKeys::currentModifiers.isShiftDown())
			tags.addRemoved(tagType, getTag());
		else
			tags.add(tagType, getTag());

		getPatchManager().modifyTags(_patches, tags);
	}

	void TagTreeItem::onParentSearchChanged(const pluginLib::patchDB::SearchRequest& _parentSearchRequest)
	{
		const auto tagType = toTagType(getGroupType());

		if(tagType == pluginLib::patchDB::TagType::Invalid)
			return;

		pluginLib::patchDB::SearchRequest sr = _parentSearchRequest;
		sr.tags.add(tagType, getTag());

		search(std::move(sr));
	}

	void TagTreeItem::itemClicked(const juce::MouseEvent& _mouseEvent)
	{
		if(_mouseEvent.mods.isPopupMenu())
		{
			const auto tagType = toTagType(getGroupType());

			if(tagType != pluginLib::patchDB::TagType::Invalid)
			{
				juce::PopupMenu menu;
				menu.addItem("Remove", [this, tagType]
				{
					getPatchManager().removeTag(tagType, m_tag);
				});

				menu.showMenuAsync({});
			}
		}
	}
}
