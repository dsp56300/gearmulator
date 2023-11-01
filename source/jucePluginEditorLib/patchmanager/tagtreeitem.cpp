#include "tagtreeitem.h"

#include "patchmanager.h"

namespace jucePluginEditorLib::patchManager
{
	TagTreeItem::TagTreeItem(PatchManager& _pm, const GroupType _type, const std::string& _tag) : TreeItem(_pm, _tag), m_group(_type), m_tag(_tag)
	{
		pluginLib::patchDB::SearchRequest sr;

		switch (_type)
		{
		case GroupType::Categories:
			sr.categories.add(_tag);
			break;
		case GroupType::Tags:
			sr.tags.add(_tag);
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
