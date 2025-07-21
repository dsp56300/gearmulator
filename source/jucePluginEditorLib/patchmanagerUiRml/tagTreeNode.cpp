#include "tagTreeNode.h"

#include "patchmanagerUiRml.h"
#include "tagsTree.h"

#include "jucePluginEditorLib/patchmanager/patchmanager.h"

#include "juceRmlUi/rmlColorPicker.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceRmlUi/rmlInterfaces.h"

#include "juceRmlUi/rmlMenu.h"

namespace juceRmlUi
{
	class ColorPicker;
}

namespace jucePluginEditorLib::patchManagerRml
{
	const std::string& TagNode::getName() const
	{
		if (getTag().empty())
		{
			static std::string uncategorizedName = "Uncategorized";
			return uncategorizedName;
		}
		return getTag();
	}

	TagTreeElem::TagTreeElem(Tree& _tree, const std::string& _rmlElemTag) : TreeElem(_tree, _rmlElemTag)
	{
	}

	void TagTreeElem::setNode(const juceRmlUi::TreeNodePtr& _node)
	{
		TreeElem::setNode(_node);

		auto* tagItem = dynamic_cast<TagNode*>(_node.get());
		if (!tagItem)
			return;

		const auto& tag = tagItem->getTag();

		setName(tagItem->getName());

		const auto tagType = toTagType(tagItem->getGroup());

		if (!tag.empty())
		{
			const auto color = tagItem->getPatchManager().getDB().getTagColor(tagType, tag);

			setColor(color);
		}

		if (tagType == pluginLib::patchDB::TagType::Favourites)
		{
			pluginLib::patchDB::SearchRequest sr;

			if (tag.empty())
				sr.noTagOfType.insert(tagType);
			else
				sr.tags.add(tagType, tag);

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
		const auto tag = getTag();

		if (tag.empty())
			sr.noTagOfType.insert(tagType);
		else
			sr.tags.add(tagType, tag);

		search(std::move(sr));
	}

	void TagTreeElem::onRightClick(const Rml::Event& _event)
	{
		TreeElem::onRightClick(_event);

		if(getTagType() != pluginLib::patchDB::TagType::Invalid && getTree()->getTree().getSelectedNodes().size() == 1)
		{
			juceRmlUi::Menu menu;
			const auto& s = getDB().getSearch(getSearchHandle());
			if(s && !s->getResultSize())
			{
				menu.addEntry("Remove", [this]
				{
					getDB().removeTag(getTagType(), getTag());
				});
			}
			menu.addEntry("Set Color...", [this]
			{
				m_colorPicker = juceRmlUi::ColorPicker::createFromTemplate("colorpicker", GetOwnerDocument(), [this](const bool _confirmed, const Rml::Colourb& _colour)
				{
					if (_confirmed)
					{
						const auto c = juceRmlUi::helper::toARGB(_colour);
						getDB().setTagColor(getTagType(), getTag(), c);
						setColor(c);
					}
					m_colorPicker.reset();
				}, juceRmlUi::helper::fromARGB(getColor()));

			});
			if(getColor() != pluginLib::patchDB::g_invalidColor)
			{
				menu.addEntry("Clear Color", [this]
				{
					getDB().setTagColor(getTagType(), getTag(), pluginLib::patchDB::g_invalidColor);
					setColor(getColor());
				});
			}

			menu.runModal(_event);
		}
	}

	bool TagTreeElem::canDropPatchList(const Rml::Event& _event, const Rml::Element* _source, const std::vector<pluginLib::patchDB::PatchPtr>& _patches) const
	{
		return hasSearch() && getTagType() != pluginLib::patchDB::TagType::Invalid && !getTag().empty();
	}

	void TagTreeElem::dropPatches(const Rml::Event& _event, const patchManager::SavePatchDesc* _data, const std::vector<pluginLib::patchDB::PatchPtr>& _patches)
	{
		if (getTagType() == pluginLib::patchDB::TagType::Invalid || getTag().empty())
			return;
		modifyTags(getDB(), getTagType(), getTag(), _patches);
	}

	void TagTreeElem::modifyTags(patchManager::PatchManager& _pm, const pluginLib::patchDB::TagType _type, const std::string& _tag, const std::vector<pluginLib::patchDB::PatchPtr>& _patches)
	{
		pluginLib::patchDB::TypedTags tags;

		if (juce::ModifierKeys::currentModifiers.isShiftDown())	// TODO: get rid of juce
			tags.addRemoved(_type, _tag);
		else
			tags.add(_type, _tag);

		_pm.modifyTags(_patches, tags);
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
