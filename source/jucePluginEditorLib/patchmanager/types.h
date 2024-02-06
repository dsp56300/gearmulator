#pragma once

namespace pluginLib::patchDB
{
	enum class TagType;
}

namespace jucePluginEditorLib::patchManager
{
	enum class GroupType
	{
		Invalid,
		DataSources,
		LocalStorage,
		Categories,
		Tags,
		Favourites,
		CustomA,
		CustomB,
		CustomC,
		Count
	};

	pluginLib::patchDB::TagType toTagType(GroupType _groupType);
	GroupType toGroupType(pluginLib::patchDB::TagType _tagType);
}
