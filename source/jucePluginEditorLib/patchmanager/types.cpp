#include "types.h"

#include "../../jucePluginLib/patchdb/patchdbtypes.h"

namespace jucePluginEditorLib::patchManager
{
	pluginLib::patchDB::TagType toTagType(const GroupType _groupType)
	{
		switch (_groupType)
		{
		case GroupType::DataSources:
		case GroupType::LocalStorage:
			return pluginLib::patchDB::TagType::Invalid;
		case GroupType::Categories:
			return pluginLib::patchDB::TagType::Category;
		case GroupType::Tags:
			return pluginLib::patchDB::TagType::Tag;
		case GroupType::Favourites:
			return pluginLib::patchDB::TagType::Favourites;
		default:
			return pluginLib::patchDB::TagType::Invalid;
		}
	}

	GroupType toGroupType(const pluginLib::patchDB::TagType _tagType)
	{
		switch (_tagType)
		{
		case pluginLib::patchDB::TagType::Category:
			return GroupType::Categories;
		case pluginLib::patchDB::TagType::Tag:
			return GroupType::Tags;
		case pluginLib::patchDB::TagType::Favourites:
			return GroupType::Favourites;
		default: 
			return GroupType::Invalid;
		}
	}
}
