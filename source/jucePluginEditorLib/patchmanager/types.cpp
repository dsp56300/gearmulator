#include "types.h"

#include "../../jucePluginLib/patchdb/patchdbtypes.h"

namespace jucePluginEditorLib::patchManager
{
	pluginLib::patchDB::TagType toTagType(const GroupType _groupType)
	{
		switch (_groupType)
		{
		case GroupType::DataSources:
		case GroupType::LocalStorage:	return pluginLib::patchDB::TagType::Invalid;
		case GroupType::Categories:		return pluginLib::patchDB::TagType::Category;
		case GroupType::Tags:			return pluginLib::patchDB::TagType::Tag;
		case GroupType::Favourites:		return pluginLib::patchDB::TagType::Favourites;
		case GroupType::CustomA:		return pluginLib::patchDB::TagType::CustomA;
		case GroupType::CustomB:		return pluginLib::patchDB::TagType::CustomB;
		case GroupType::CustomC:		return pluginLib::patchDB::TagType::CustomC;
		default:						return pluginLib::patchDB::TagType::Invalid;
		}
	}

	GroupType toGroupType(const pluginLib::patchDB::TagType _tagType)
	{
		switch (_tagType)
		{
		case pluginLib::patchDB::TagType::Category:		return GroupType::Categories;
		case pluginLib::patchDB::TagType::Tag:			return GroupType::Tags;
		case pluginLib::patchDB::TagType::Favourites:	return GroupType::Favourites;
		case pluginLib::patchDB::TagType::CustomA:		return GroupType::CustomA;
		case pluginLib::patchDB::TagType::CustomB:		return GroupType::CustomB;
		case pluginLib::patchDB::TagType::CustomC:		return GroupType::CustomC;
		default:										return GroupType::Invalid;
		}
	}
}
