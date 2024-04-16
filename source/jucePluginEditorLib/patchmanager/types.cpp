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
		case GroupType::Factory:		return pluginLib::patchDB::TagType::Invalid;
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

	GroupType toGroupType(const pluginLib::patchDB::SourceType _sourceType)
	{
		switch (_sourceType)
		{
		case pluginLib::patchDB::SourceType::Rom:			return GroupType::Factory;
		case pluginLib::patchDB::SourceType::LocalStorage:	return GroupType::LocalStorage;
		case pluginLib::patchDB::SourceType::Folder:
		case pluginLib::patchDB::SourceType::File:			return GroupType::DataSources;
		case pluginLib::patchDB::SourceType::Invalid:
		case pluginLib::patchDB::SourceType::Count:
		default:											return GroupType::Invalid;
		}
	}

	pluginLib::patchDB::SourceType toSourceType(const GroupType _groupType)
	{
		switch (_groupType)
		{
		case GroupType::DataSources:	return pluginLib::patchDB::SourceType::File;
		case GroupType::LocalStorage:	return pluginLib::patchDB::SourceType::LocalStorage;
		case GroupType::Factory:		return pluginLib::patchDB::SourceType::Rom;
		default:						return pluginLib::patchDB::SourceType::Invalid;
		}
	}
}
