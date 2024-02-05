#include "tagtreeitem.h"

#include "patchmanager.h"
#include "tree.h"
#include "juce_gui_extra/misc/juce_ColourSelector.h"

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
		getPatchManager().repaint();
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
				const auto& s = getPatchManager().getSearch(getSearchHandle());
				if(s && !s->getResultSize())
				{
					menu.addItem("Remove", [this, tagType]
					{
						getPatchManager().removeTag(tagType, m_tag);
					});
				}
				menu.addItem("Set Color...", [this, tagType]
				{
					juce::ColourSelector* cs = new juce::ColourSelector(juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders | juce::ColourSelector::showColourspace);

					cs->getProperties().set("tagType", static_cast<int>(tagType));
					cs->getProperties().set("tag", juce::String(getTag()));

					cs->setSize(400,300);
					cs->setCurrentColour(juce::Colour(getColor()));
					cs->addChangeListener(&getPatchManager());

					const auto treeRect = getTree()->getScreenBounds();
					const auto itemRect = getItemPosition(true);
					auto rect = itemRect;
					rect.translate(treeRect.getX(), treeRect.getY());

					juce::CallOutBox::launchAsynchronously(std::unique_ptr<juce::Component>(cs), rect, nullptr);
				});
				if(getColor() != pluginLib::patchDB::g_invalidColor)
				{
					menu.addItem("Clear Color", [this, tagType]
					{
						getPatchManager().setTagColor(tagType, getTag(), pluginLib::patchDB::g_invalidColor);
						getPatchManager().repaint();
					});
				}

				menu.showMenuAsync({});
			}
		}
	}

	pluginLib::patchDB::Color TagTreeItem::getColor() const
	{
		const auto tagType = toTagType(getGroupType());
		if(tagType != pluginLib::patchDB::TagType::Invalid)
			return getPatchManager().getTagColor(tagType, getTag());
		return TreeItem::getColor();
	}
}
