#include "tagTreeNode.h"

#include "patchmanagerUiRml.h"
#include "tagsTree.h"

#include "jucePluginEditorLib/patchmanager/patchmanager.h"

#include "juceRmlUi/rmlMenu.h"

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

		const auto color = tagItem->getPatchManager().getDB().getTagColor(tagType, tagItem->getTag());

		setColor(color);

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

		const auto tagType = getTagType();

		if(tagType == pluginLib::patchDB::TagType::Invalid)
			return;

		pluginLib::patchDB::SearchRequest sr = _parentSearchRequest;
		sr.tags.add(tagType, getTag());

		search(std::move(sr));
	}

	void TagTreeElem::onRightClick(const Rml::Event& _event)
	{
		TreeElem::onRightClick(_event);

		if(getTagType() != pluginLib::patchDB::TagType::Invalid && getTree()->getTree().getSelectedNodes().size() == 1)
		{
			juce::PopupMenu menu;
			const auto& s = getDB().getSearch(getSearchHandle());
			if(s && !s->getResultSize())
			{
				menu.addItem("Remove", [this]
				{
					getDB().removeTag(getTagType(), getTag());
				});
			}
			menu.addItem("Set Color...", [this]
			{
				/*
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
				*/
				setColor(getColor());
			});
			if(getColor() != pluginLib::patchDB::g_invalidColor)
			{
				menu.addItem("Clear Color", [this]
				{
					getDB().setTagColor(getTagType(), getTag(), pluginLib::patchDB::g_invalidColor);
					setColor(getColor());
				});
			}

			menu.showMenuAsync({});
		}
	}

	pluginLib::patchDB::Tag TagTreeElem::getTag() const
	{
		auto* tagItem = dynamic_cast<TagNode*>(getNode().get());
		if (!tagItem)
			return {};
		return tagItem->getTag();
	}

	pluginLib::patchDB::TagType TagTreeElem::getTagType() const
	{
		auto* tagItem = dynamic_cast<TagNode*>(getNode().get());
		if (!tagItem)
			return pluginLib::patchDB::TagType::Invalid;
		return toTagType(tagItem->getGroup());
	}

	pluginLib::patchDB::Color TagTreeElem::getColor() const
	{
		auto* tagItem = dynamic_cast<TagNode*>(getNode().get());
		if (!tagItem)
			return pluginLib::patchDB::g_invalidColor;
		return tagItem->getPatchManager().getDB().getTagColor(getTagType(), tagItem->getTag());
	}
}
