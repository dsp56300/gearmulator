#include "types.h"

#include "../../jucePluginLib/patchdb/patchdbtypes.h"

namespace jucePluginEditorLib::patchManager
{
	pluginLib::patchDB::TagType toTagType(const GroupType _groupType)
	{
		switch (_groupType)
		{
		case GroupType::DataSources:
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
}
