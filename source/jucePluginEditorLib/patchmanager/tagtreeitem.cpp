#include "tagtreeitem.h"

#include "patchmanager.h"
#include "patchmanageruijuce.h"
#include "tree.h"

#if JUCE_MAJOR_VERSION < 8	// they forgot this include but fixed it in version 8+
#include "juce_gui_extra/misc/juce_ColourSelector.h"
#endif

namespace jucePluginEditorLib::patchManager
{
	TagTreeItem::TagTreeItem(PatchManagerUiJuce& _pm, const GroupType _type, const std::string& _tag) : TreeItem(_pm, _tag), m_group(_type), m_tag(_tag)
	{
		const auto tagType = toTagType(getGroupType());

		if(tagType == pluginLib::patchDB::TagType::Favourites)
		{
			pluginLib::patchDB::SearchRequest sr;
			sr.tags.add(tagType, getTag());

			search(std::move(sr));
		}

		setDeselectonSecondClick(true);
	}

	bool TagTreeItem::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails)
	{
		return TreeItem::isInterestedInDragSource(dragSourceDetails) && hasSearch();
	}

	bool TagTreeItem::isInterestedInPatchList(const ListModel*, const std::vector<pluginLib::patchDB::PatchPtr>&)
	{
		return hasSearch() && toTagType(getGroupType()) != pluginLib::patchDB::TagType::Invalid;
	}

	void TagTreeItem::patchesDropped(const std::vector<pluginLib::patchDB::PatchPtr>& _patches, const SavePatchDesc*/* _savePatchDesc = nullptr*/)
	{
		const auto tagType = toTagType(getGroupType());

		if (tagType == pluginLib::patchDB::TagType::Invalid)
			return;

		modifyTags(getPatchManager(), tagType, getTag(), _patches);
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

	void TagTreeItem::modifyTags(PatchManagerUiJuce& _pm, pluginLib::patchDB::TagType _type, const std::string& _tag, const std::vector<pluginLib::patchDB::PatchPtr>& _patches)
	{
		_pm.repaint();
	}

	void TagTreeItem::itemClicked(const juce::MouseEvent& _mouseEvent)
	{
		TreeItem::itemClicked(_mouseEvent);
	}

	pluginLib::patchDB::Color TagTreeItem::getColor() const
	{
		const auto tagType = toTagType(getGroupType());
		if(tagType != pluginLib::patchDB::TagType::Invalid)
			return getDB().getTagColor(tagType, getTag());
		return TreeItem::getColor();
	}
}
