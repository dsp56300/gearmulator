#include "tagtreeitem.h"

#include <cassert>

#include "patchmanager.h"

namespace jucePluginEditorLib::patchManager
{
	TagTreeItem::TagTreeItem(PatchManager& _pm, const GroupType _type, const std::string& _tag) : TreeItem(_pm, _tag), m_group(_type), m_tag(_tag)
	{
		pluginLib::patchDB::SearchRequest sr;

		switch (_type)
		{
		case GroupType::Categories:
			sr.tags.add(pluginLib::patchDB::TagType::Category, _tag);
			break;
		case GroupType::Tags:
			sr.tags.add(pluginLib::patchDB::TagType::Tag, _tag);
			break;
		case GroupType::Favourites:
			sr.tags.add(pluginLib::patchDB::TagType::Favourites, _tag);
			break;
		default:
			assert(false);
			break;
		}

		search(std::move(sr));
	}

	void TagTreeItem::processSearchUpdated(const pluginLib::patchDB::Search& _search)
	{
		const auto patchCount = _search.getResultSize();

		const auto title = m_tag + " (" + std::to_string(patchCount) + ")";
		setTitle(title);
	}
}
